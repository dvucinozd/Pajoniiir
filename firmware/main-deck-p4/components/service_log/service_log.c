#include "service_log.h"
#include "service_log_format.h"
#include "sd_io_gate.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_DIR        "/sd/logs"
#define LOG_BASE       "/sd/logs/system.log"
#define WRITER_STACK   4096
#define WRITER_PRIO    2
#define WRITER_WAIT_MS 2000
#define WRITER_BATCH   32u
#define SYNC_PERIOD_US (5ll * 1000000ll)

static QueueHandle_t s_queue = NULL;
static TaskHandle_t  s_writer = NULL;
static uint32_t      s_seq = 0u;
static uint32_t      s_boot_id = 0u;
static char          s_fw_version[SERVICE_LOG_TEXT_MAX] = "n/a";
static char          s_partition[SERVICE_LOG_TEXT_MAX] = "n/a";
static char          s_reset_reason[SERVICE_LOG_TEXT_MAX] = "n/a";

/* Producer-visible counters (atomic). */
static uint32_t s_dropped = 0u;

/* Writer-owned state. */
static FILE     *s_fp = NULL;
static uint64_t  s_current_bytes = 0u;
static uint32_t  s_written = 0u;
static esp_err_t s_last_error = ESP_OK;
static bool      s_available = false;
static bool      s_dirty = false;
static bool      s_header_written = false;
static int64_t   s_last_sync_us = 0;

static uint32_t load_boot_id(void)
{
    nvs_handle_t h;
    uint32_t id = 0u;
    if (nvs_open("svc_log", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_get_u32(h, "boot_id", &id);
        id++;
        if (id == 0u) {
            id = 1u;
        }
        if (nvs_set_u32(h, "boot_id", id) == ESP_OK) {
            (void)nvs_commit(h);
        }
        nvs_close(h);
    }
    return id;   /* 0 is acceptable if NVS is unavailable */
}

static bool sd_ready(void)
{
    struct stat st;
    return stat("/sd", &st) == 0 && S_ISDIR(st.st_mode);
}

static void gen_path(char *out, size_t out_len, unsigned generation)
{
    if (generation == 0u) {
        snprintf(out, out_len, "%s", LOG_BASE);
    } else {
        snprintf(out, out_len, "%s.%u", LOG_BASE, generation);
    }
}

/* Shift .2->.3, .1->.2, base->.1 and start a fresh base. Caller holds the gate
 * and s_fp is closed. */
static void rotate_generations(void)
{
    char from[64], to[64];
    if (SERVICE_LOG_GENERATIONS >= 2u) {
        gen_path(to, sizeof(to), SERVICE_LOG_GENERATIONS - 1u);
        unlink(to);
        for (unsigned g = SERVICE_LOG_GENERATIONS - 1u; g >= 1u; g--) {
            gen_path(from, sizeof(from), g - 1u);
            gen_path(to, sizeof(to), g);
            rename(from, to);
        }
    }
}

/* Open (or reopen) the active generation for append; writes the boot header on
 * a fresh/empty file. Caller holds the gate. */
static esp_err_t open_active(void)
{
    if (mkdir(LOG_DIR, 0775) != 0 && errno != EEXIST) {
        s_last_error = ESP_FAIL;
        return ESP_FAIL;
    }
    s_fp = fopen(LOG_BASE, "a");
    if (!s_fp) {
        s_last_error = ESP_FAIL;
        return ESP_FAIL;
    }
    struct stat st;
    s_current_bytes = (stat(LOG_BASE, &st) == 0) ? (uint64_t)st.st_size : 0u;
    /* One header per boot, plus one at the top of every fresh generation, so a
     * rotated file is self-describing even mid-session. */
    if (!s_header_written || s_current_bytes == 0u) {
        char hdr[SERVICE_LOG_LINE_MAX];
        int n = service_log_format_header(hdr, sizeof(hdr), s_boot_id,
                                          s_fw_version, s_partition, s_reset_reason);
        if (n > 0 && fwrite(hdr, 1, (size_t)n, s_fp) == (size_t)n) {
            s_current_bytes += (uint64_t)n;
        }
        s_header_written = true;
    }
    return ESP_OK;
}

static void close_active(void)
{
    if (s_fp) {
        fflush(s_fp);
        fsync(fileno(s_fp));
        fclose(s_fp);
        s_fp = NULL;
    }
}

/* Format and append one record. Caller holds the gate. */
static void write_record(const service_log_record_t *rec)
{
    if (!s_fp) {
        return;
    }
    char line[SERVICE_LOG_LINE_MAX];
    int n = service_log_format_record(line, sizeof(line), rec);
    if (n <= 0) {
        return;
    }
    if (service_log_should_rotate(s_current_bytes + (uint64_t)n, SERVICE_LOG_MAX_FILE_BYTES)) {
        close_active();
        rotate_generations();
        if (open_active() != ESP_OK) {
            return;
        }
    }
    if (fwrite(line, 1, (size_t)n, s_fp) == (size_t)n) {
        s_current_bytes += (uint64_t)n;
        s_written++;
        s_dirty = true;
    } else {
        s_last_error = ESP_FAIL;
    }
}

static void flush_if_dirty(bool force_sync)
{
    if (!s_fp || !s_dirty) {
        return;
    }
    sd_io_gate_begin();
    fflush(s_fp);
    int64_t now = esp_timer_get_time();
    if (force_sync || now - s_last_sync_us >= SYNC_PERIOD_US) {
        fsync(fileno(s_fp));
        s_last_sync_us = now;
        s_dirty = false;
    }
    sd_io_gate_end();
}

static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        service_log_record_t rec;
        if (xQueueReceive(s_queue, &rec, pdMS_TO_TICKS(WRITER_WAIT_MS)) == pdTRUE) {
            if (!s_fp && sd_ready()) {
                sd_io_gate_begin();
                if (open_active() == ESP_OK) {
                    s_available = true;
                }
                sd_io_gate_end();
            }
            if (s_fp) {
                sd_io_gate_begin();
                write_record(&rec);
                uint32_t batch = 1u;
                while (batch < WRITER_BATCH &&
                       xQueueReceive(s_queue, &rec, 0) == pdTRUE) {
                    write_record(&rec);
                    batch++;
                }
                sd_io_gate_end();
                flush_if_dirty(false);
            }
            /* If /sd is not ready the record is simply dropped by draining. */
        } else {
            flush_if_dirty(false);
        }
    }
}

esp_err_t service_log_init(const char *fw_version, const char *partition,
                           const char *reset_reason)
{
    if (s_queue) {
        return ESP_OK;
    }
    if (fw_version)   service_log_sanitize(s_fw_version, sizeof(s_fw_version), fw_version);
    if (partition)    service_log_sanitize(s_partition, sizeof(s_partition), partition);
    if (reset_reason) service_log_sanitize(s_reset_reason, sizeof(s_reset_reason), reset_reason);

    s_queue = xQueueCreate(SERVICE_LOG_QUEUE_LEN, sizeof(service_log_record_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    s_boot_id = load_boot_id();
    s_last_sync_us = esp_timer_get_time();
    if (xTaskCreate(writer_task, "svc_log", WRITER_STACK, NULL, WRITER_PRIO,
                    &s_writer) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    service_log_note(SERVICE_LOG_BOOT, SERVICE_LOG_INFO, "main deck online");
    return ESP_OK;
}

void service_log_event(service_log_event_t event, service_log_severity_t sev,
                       uint8_t arg_count, uint32_t a0, uint32_t a1,
                       uint32_t a2, uint32_t a3, const char *text)
{
    if (!s_queue) {
        return;
    }
    service_log_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.seq = __atomic_add_fetch(&s_seq, 1u, __ATOMIC_RELAXED);
    rec.boot_id = s_boot_id;
    rec.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    rec.event = event;
    rec.severity = sev;
    rec.arg_count = arg_count > SERVICE_LOG_ARG_MAX ? SERVICE_LOG_ARG_MAX : arg_count;
    rec.args[0] = a0; rec.args[1] = a1; rec.args[2] = a2; rec.args[3] = a3;
    if (text) {
        service_log_sanitize(rec.text, sizeof(rec.text), text);
    }
    if (xQueueSend(s_queue, &rec, 0) != pdTRUE) {
        __atomic_add_fetch(&s_dropped, 1u, __ATOMIC_RELAXED);
    }
}

esp_err_t service_log_get_status(service_log_status_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->available = s_available;
    out->queue_capacity = SERVICE_LOG_QUEUE_LEN;
    out->queue_depth = s_queue ? (uint32_t)uxQueueMessagesWaiting(s_queue) : 0u;
    out->dropped = __atomic_load_n(&s_dropped, __ATOMIC_RELAXED);
    out->written = s_written;
    out->current_bytes = s_current_bytes;
    out->last_error = s_last_error;
    return ESP_OK;
}

void service_log_sync(void)
{
    flush_if_dirty(true);
}
