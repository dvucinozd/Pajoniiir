#include "controller_profile_manager.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Pure helpers (host-testable) ──────────────────────────────────────────── */

uint32_t controller_profile_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

esp_err_t controller_profile_meta_parse(const uint8_t *data, size_t len,
                                        controller_profile_meta_t *meta)
{
    if (!data || !meta) {
        return ESP_ERR_INVALID_ARG;
    }

    meta->valid = false;
    meta->vid = 0;
    meta->pid = 0;
    meta->input_count = 0;
    meta->output_count = 0;
    meta->size = 0;

    if (len < CPM_HEADER_SIZE || len > CPM_MAX_PROFILE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (memcmp(data, CPM_MAGIC, 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rd_u16(data + 4) != CPM_VERSION || rd_u16(data + 6) != CPM_HEADER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t profile_size = rd_u32(data + 8);
    if (profile_size != len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (controller_profile_crc32(data + 16, len - 16) != rd_u32(data + 12)) {
        return ESP_ERR_INVALID_ARG;
    }

    meta->vid = rd_u16(data + 16);
    meta->pid = rd_u16(data + 18);
    meta->input_count = rd_u16(data + 24);
    meta->output_count = rd_u16(data + 26);
    meta->size = profile_size;
    meta->valid = true;
    return ESP_OK;
}

/* Read <dir>/profile.s3bin (bounded) and header-validate it into *meta. */
static bool load_profile_meta(const char *dir_path, const char *name,
                              controller_profile_meta_t *meta)
{
    char path[CPM_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s/profile.s3bin", dir_path, name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false; /* not a profile directory */
    }

    uint8_t *buf = (uint8_t *)malloc(CPM_MAX_PROFILE_SIZE + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t len = fread(buf, 1, CPM_MAX_PROFILE_SIZE + 1, f);
    fclose(f);

    memset(meta, 0, sizeof(*meta));
    /* Directory names longer than the id field are truncated deliberately. */
    snprintf(meta->id, sizeof(meta->id), "%.*s", (int)sizeof(meta->id) - 1, name);
    memcpy(meta->path, path, sizeof(meta->path));
    meta->path[sizeof(meta->path) - 1] = '\0';
    /* A too-large file fails validation inside meta_parse (len bound). */
    (void)controller_profile_meta_parse(buf, len, meta);
    free(buf);
    return true; /* recorded (possibly with valid=false so UI can report it) */
}

esp_err_t controller_profile_scan_dir(const char *root,
                                      controller_profile_registry_t *reg)
{
    if (!root || !reg) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reg, 0, sizeof(*reg));
    reg->active_index = -1;

    DIR *dir = opendir(root);
    if (!dir) {
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && reg->count < CPM_MAX_PROFILES) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        controller_profile_meta_t meta;
        if (load_profile_meta(root, entry->d_name, &meta)) {
            reg->profiles[reg->count++] = meta;
        }
    }
    closedir(dir);
    return ESP_OK;
}

int controller_profile_registry_match(const controller_profile_registry_t *reg,
                                      uint16_t vid, uint16_t pid)
{
    if (!reg) {
        return -1;
    }
    for (uint8_t i = 0; i < reg->count; i++) {
        if (reg->profiles[i].valid &&
            reg->profiles[i].vid == vid && reg->profiles[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

int controller_profile_registry_on_descriptor(controller_profile_registry_t *reg,
                                              uint16_t vid, uint16_t pid)
{
    if (!reg) {
        return -1;
    }
    reg->controller_present = true;
    reg->connected_vid = vid;
    reg->connected_pid = pid;
    int idx = controller_profile_registry_match(reg, vid, pid);
    reg->active_index = (int8_t)idx;
    return idx;
}

/* ── Firmware glue (ESP-IDF only) ──────────────────────────────────────────── */

#ifndef CONTROLLER_PROFILE_MANAGER_PC_TEST

#include "control_link.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifndef CONFIG_CONTROLLER_PROFILE_SD_PATH
#define CONFIG_CONTROLLER_PROFILE_SD_PATH "/sd/controllers"
#endif

#define CPM_SENDER_STACK      4096
#define CPM_SEND_ATTEMPTS     3
#define CPM_REPLY_TIMEOUT_MS  2000
#define CPM_CHUNK_PACE_MS     2

static const char *TAG = "ctrl_profile";

static controller_profile_registry_t s_registry;

/* Profile-transfer sender: runs off the control-link RX task so the multi-KB
 * stream to the S3 never blocks event/descriptor handling. */
static QueueHandle_t s_send_q;
static SemaphoreHandle_t s_reply_sem;
static volatile bool s_reply_ack;
static volatile uint8_t s_reply_ref;
static volatile uint8_t s_reply_reason;

static void cpm_on_reply(bool ack, uint8_t ref_type, uint8_t reason)
{
    s_reply_ack = ack;
    s_reply_ref = ref_type;
    s_reply_reason = reason;
    if (s_reply_sem) {
        xSemaphoreGive(s_reply_sem);
    }
}

static bool cpm_wait_reply(uint8_t expect_ref)
{
    if (xSemaphoreTake(s_reply_sem, pdMS_TO_TICKS(CPM_REPLY_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "profile reply timeout (expected 0x%02X)", expect_ref);
        return false;
    }
    return s_reply_ack && s_reply_ref == expect_ref;
}

static bool cpm_stream_profile(const controller_profile_meta_t *m)
{
    FILE *f = fopen(m->path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s", m->path);
        return false;
    }
    uint8_t *buf = malloc(m->size);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t got = fread(buf, 1, m->size, f);
    fclose(f);
    if (got != m->size) {
        free(buf);
        return false;
    }

    uint32_t crc = cp_xfer_crc32(buf, m->size);
    bool ok = false;

    for (int attempt = 1; attempt <= CPM_SEND_ATTEMPTS && !ok; attempt++) {
        while (xSemaphoreTake(s_reply_sem, 0) == pdTRUE) {
            /* drain stale replies before starting a fresh transfer */
        }
        if (control_link_send_profile_begin((uint32_t)m->size, crc,
                                            m->vid, m->pid) != ESP_OK ||
            !cpm_wait_reply(CTRL_BULK_TYPE_PROFILE_BEGIN)) {
            continue;
        }
        bool chunks_ok = true;
        for (uint32_t off = 0; off < m->size; off += CTRL_PROFILE_CHUNK_MAX) {
            size_t n = m->size - off;
            if (n > CTRL_PROFILE_CHUNK_MAX) {
                n = CTRL_PROFILE_CHUNK_MAX;
            }
            if (control_link_send_profile_chunk(off, buf + off, n) != ESP_OK) {
                chunks_ok = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(CPM_CHUNK_PACE_MS));
        }
        if (!chunks_ok) {
            continue;
        }
        if (control_link_send_profile_simple(CTRL_BULK_TYPE_PROFILE_END) != ESP_OK ||
            !cpm_wait_reply(CTRL_BULK_TYPE_PROFILE_END)) {
            continue;
        }
        if (control_link_send_profile_simple(CTRL_BULK_TYPE_PROFILE_ACTIVATE) != ESP_OK ||
            !cpm_wait_reply(CTRL_BULK_TYPE_PROFILE_ACTIVATE)) {
            continue;
        }
        ok = true;
    }

    free(buf);
    return ok;
}

static void cpm_sender_task(void *arg)
{
    (void)arg;
    for (;;) {
        int idx;
        if (xQueueReceive(s_send_q, &idx, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (idx < 0 || idx >= (int)s_registry.count) {
            continue;
        }
        controller_profile_meta_t m = s_registry.profiles[idx];
        if (!m.valid) {
            continue;
        }
        bool ok = cpm_stream_profile(&m);
        ESP_LOGI(TAG, "profile '%s' transfer to S3 %s", m.id,
                 ok ? "OK" : "FAILED");
    }
}

esp_err_t controller_profile_manager_init(void)
{
    memset(&s_registry, 0, sizeof(s_registry));
    s_registry.active_index = -1;

    if (!s_reply_sem) {
        s_reply_sem = xSemaphoreCreateBinary();
        s_send_q = xQueueCreate(2, sizeof(int));
        if (!s_reply_sem || !s_send_q) {
            return ESP_ERR_NO_MEM;
        }
        if (xTaskCreate(cpm_sender_task, "cpm_send", CPM_SENDER_STACK, NULL, 4,
                        NULL) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
        control_link_set_profile_reply_cb(cpm_on_reply);
    }
    return ESP_OK;
}

esp_err_t controller_profile_manager_scan_storage(void)
{
    esp_err_t rc = controller_profile_scan_dir(CONFIG_CONTROLLER_PROFILE_SD_PATH,
                                               &s_registry);
    if (rc == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no %s directory (SD missing or no profiles)",
                 CONFIG_CONTROLLER_PROFILE_SD_PATH);
        return rc;
    }
    if (rc != ESP_OK) {
        return rc;
    }

    ESP_LOGI(TAG, "%u controller profile(s) in %s",
             (unsigned)s_registry.count, CONFIG_CONTROLLER_PROFILE_SD_PATH);
    for (uint8_t i = 0; i < s_registry.count; i++) {
        const controller_profile_meta_t *m = &s_registry.profiles[i];
        if (m->valid) {
            ESP_LOGI(TAG,
                     "  [%u] %s VID=0x%04X PID=0x%04X inputs=%u outputs=%u (%u B)",
                     (unsigned)i, m->id, m->vid, m->pid,
                     (unsigned)m->input_count, (unsigned)m->output_count,
                     (unsigned)m->size);
        } else {
            ESP_LOGW(TAG, "  [%u] %s INVALID (bad header/CRC) at %s",
                     (unsigned)i, m->id, m->path);
        }
    }
    return ESP_OK;
}

const controller_profile_registry_t *controller_profile_manager_get_registry(void)
{
    return &s_registry;
}

int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid)
{
    int idx = controller_profile_registry_on_descriptor(&s_registry, vid, pid);
    if (idx >= 0) {
        ESP_LOGI(TAG, "controller VID=0x%04X PID=0x%04X -> profile '%s'",
                 vid, pid, s_registry.profiles[idx].id);
        /* Hand the transfer to the sender task; drop silently if it is busy
         * (the next connect/heartbeat descriptor re-triggers a match). */
        if (s_send_q) {
            (void)xQueueSend(s_send_q, &idx, 0);
        }
    } else {
        ESP_LOGW(TAG, "controller VID=0x%04X PID=0x%04X has no profile", vid, pid);
    }
    return idx;
}

int controller_profile_manager_on_descriptor_report(uint16_t vid, uint16_t pid,
                                                    uint16_t caps,
                                                    const char *product)
{
    s_registry.connected_caps = caps;
    memset(s_registry.connected_product, 0, sizeof(s_registry.connected_product));
    if (product) {
        snprintf(s_registry.connected_product, sizeof(s_registry.connected_product),
                 "%.*s", CPM_PRODUCT_MAX, product);
    }
    ESP_LOGI(TAG, "connected controller '%s' caps=0x%04X",
             s_registry.connected_product, caps);
    return controller_profile_manager_on_descriptor(vid, pid);
}

#endif /* CONTROLLER_PROFILE_MANAGER_PC_TEST */
