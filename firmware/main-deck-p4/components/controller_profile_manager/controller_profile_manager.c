#if defined(CONTROLLER_PROFILE_MANAGER_PC_TEST) && !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "controller_profile_manager.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

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

    uint16_t input_count = rd_u16(data + 24);
    uint16_t output_count = rd_u16(data + 26);
    uint8_t pair_slot_count = data[28];
    if (input_count > CPM_MAX_INPUTS || output_count > CPM_MAX_OUTPUTS ||
        pair_slot_count > CPM_MAX_PAIR_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t expected_size = CPM_HEADER_SIZE +
                           (size_t)input_count * CPM_INPUT_ENTRY_SIZE +
                           (size_t)output_count * CPM_OUTPUT_ENTRY_SIZE;
    if (expected_size != len) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *entry = data + CPM_HEADER_SIZE;
    for (uint16_t i = 0; i < input_count;
         i++, entry += CPM_INPUT_ENTRY_SIZE) {
        uint8_t raw_type = entry[2];
        uint8_t pair_slot = entry[3];
        if (raw_type > CPM_MAX_RAW_TYPE) {
            return ESP_ERR_INVALID_ARG;
        }
        bool needs_pair_slot = raw_type == 4 || raw_type == 5 || raw_type == 7;
        if (needs_pair_slot &&
            (pair_slot == CPM_PAIR_SLOT_NONE || pair_slot >= pair_slot_count)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (uint16_t i = 0; i < output_count;
         i++, entry += CPM_OUTPUT_ENTRY_SIZE) {
        if (entry[2] > CPM_MAX_OUTPUT_KIND) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    meta->vid = rd_u16(data + 16);
    meta->pid = rd_u16(data + 18);
    meta->input_count = input_count;
    meta->output_count = output_count;
    meta->size = profile_size;
    meta->valid = true;
    return ESP_OK;
}

bool controller_profile_id_valid(const char *id)
{
    if (!id) {
        return false;
    }
    size_t len = strlen(id);
    if (len == 0 || len >= CPM_ID_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool build_storage_path(char *out, size_t out_size, const char *root,
                               const char *id, const char *filename)
{
    int n = snprintf(out, out_size, "%s/%s%s%s", root, id,
                     filename ? "/" : "", filename ? filename : "");
    return n >= 0 && (size_t)n < out_size;
}

static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static esp_err_t validate_profile_file(const char *path,
                                       controller_profile_meta_t *meta)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t *buf = (uint8_t *)malloc(CPM_MAX_PROFILE_SIZE + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t len = fread(buf, 1, CPM_MAX_PROFILE_SIZE + 1, f);
    bool read_ok = !ferror(f);
    fclose(f);
    esp_err_t rc = read_ok
                       ? controller_profile_meta_parse(buf, len, meta)
                       : ESP_FAIL;
    free(buf);
    return rc;
}

static esp_err_t ensure_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    if (errno != ENOENT) {
        return ESP_FAIL;
    }
#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = mkdir(path, 0775);
#endif
    if (rc == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t sync_file(FILE *f)
{
    if (fflush(f) != 0) {
        return ESP_FAIL;
    }
#ifdef _WIN32
    return _commit(_fileno(f)) == 0 ? ESP_OK : ESP_FAIL;
#else
    return fsync(fileno(f)) == 0 ? ESP_OK : ESP_FAIL;
#endif
}

esp_err_t controller_profile_storage_recover(const char *root, const char *id)
{
    char target[CPM_PATH_MAX];
    char upload[CPM_PATH_MAX];
    char backup[CPM_PATH_MAX];
    if (!root || !controller_profile_id_valid(id) ||
        !build_storage_path(target, sizeof(target), root, id,
                            CPM_PROFILE_FILENAME) ||
        !build_storage_path(upload, sizeof(upload), root, id,
                            CPM_UPLOAD_FILENAME) ||
        !build_storage_path(backup, sizeof(backup), root, id,
                            CPM_BACKUP_FILENAME)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (path_exists(target)) {
        controller_profile_meta_t target_meta = {0};
        if (validate_profile_file(target, &target_meta) != ESP_OK &&
            path_exists(backup)) {
            controller_profile_meta_t backup_meta = {0};
            if (validate_profile_file(backup, &backup_meta) == ESP_OK) {
                if (remove(target) != 0 || rename(backup, target) != 0) {
                    return ESP_FAIL;
                }
            }
        }
        (void)remove(upload);
        (void)remove(backup);
        return ESP_OK;
    }
    if (path_exists(backup) && rename(backup, target) != 0) {
        return ESP_FAIL;
    }
    (void)remove(upload);
    return ESP_OK;
}

esp_err_t controller_profile_storage_install(const char *root, const char *id,
                                             const uint8_t *data, size_t len,
                                             bool overwrite,
                                             controller_profile_meta_t *out_meta)
{
    controller_profile_meta_t parsed = {0};
    if (!root || !controller_profile_id_valid(id) || !data ||
        controller_profile_meta_parse(data, len, &parsed) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    char profile_dir[CPM_PATH_MAX];
    char target[CPM_PATH_MAX];
    char upload[CPM_PATH_MAX];
    char backup[CPM_PATH_MAX];
    if (!build_storage_path(profile_dir, sizeof(profile_dir), root, id, NULL) ||
        !build_storage_path(target, sizeof(target), root, id,
                            CPM_PROFILE_FILENAME) ||
        !build_storage_path(upload, sizeof(upload), root, id,
                            CPM_UPLOAD_FILENAME) ||
        !build_storage_path(backup, sizeof(backup), root, id,
                            CPM_BACKUP_FILENAME)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ensure_directory(root) != ESP_OK ||
        ensure_directory(profile_dir) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_err_t rc = controller_profile_storage_recover(root, id);
    if (rc != ESP_OK) {
        return rc;
    }

    bool had_target = path_exists(target);
    if (had_target && !overwrite) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(upload, "wb");
    if (!f) {
        return ESP_FAIL;
    }
    bool write_ok = fwrite(data, 1, len, f) == len && sync_file(f) == ESP_OK;
    if (fclose(f) != 0) {
        write_ok = false;
    }
    if (!write_ok) {
        (void)remove(upload);
        return ESP_FAIL;
    }

    controller_profile_meta_t disk_meta = {0};
    rc = validate_profile_file(upload, &disk_meta);
    if (rc != ESP_OK) {
        (void)remove(upload);
        return rc;
    }

    if (had_target) {
        (void)remove(backup);
        if (rename(target, backup) != 0) {
            (void)remove(upload);
            return ESP_FAIL;
        }
    }
    if (rename(upload, target) != 0) {
        if (had_target) {
            (void)rename(backup, target);
        }
        (void)remove(upload);
        return ESP_FAIL;
    }

    memset(&disk_meta, 0, sizeof(disk_meta));
    rc = validate_profile_file(target, &disk_meta);
    if (rc != ESP_OK) {
        (void)remove(target);
        if (had_target) {
            (void)rename(backup, target);
        }
        return rc;
    }
    (void)remove(backup);

    snprintf(disk_meta.id, sizeof(disk_meta.id), "%s", id);
    snprintf(disk_meta.path, sizeof(disk_meta.path), "%s", target);
    if (out_meta) {
        *out_meta = disk_meta;
    }
    return ESP_OK;
}

/* Read <dir>/profile.s3bin (bounded) and header-validate it into *meta. */
static bool load_profile_meta(const char *dir_path, const char *name,
                              controller_profile_meta_t *meta)
{
    char path[CPM_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s/%s", dir_path, name,
                     CPM_PROFILE_FILENAME);
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
    reg->matched_index = -1;
    reg->active_index = -1;
    reg->transfer_state = CPM_TRANSFER_IDLE;

    DIR *dir = opendir(root);
    if (!dir) {
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && reg->count < CPM_MAX_PROFILES) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!controller_profile_id_valid(entry->d_name)) {
            continue;
        }
        if (controller_profile_storage_recover(root, entry->d_name) != ESP_OK) {
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
    reg->matched_index = (int8_t)idx;
    if (idx >= 0) {
        if (reg->active_index == idx &&
            reg->transfer_state == CPM_TRANSFER_ACTIVE) {
            return idx;
        }
        reg->active_index = -1;
        reg->transfer_state = CPM_TRANSFER_MATCHED;
    } else {
        reg->active_index = -1;
        reg->transfer_state = CPM_TRANSFER_UNSUPPORTED;
    }
    return idx;
}

bool controller_profile_registry_on_disconnect(controller_profile_registry_t *reg)
{
    if (!reg) {
        return false;
    }
    bool was_present = reg->controller_present;
    reg->controller_present = false;
    reg->connected_vid = 0u;
    reg->connected_pid = 0u;
    reg->connected_caps = 0u;
    memset(reg->connected_product, 0, sizeof(reg->connected_product));
    reg->matched_index = -1;
    reg->active_index = -1;
    reg->transfer_state = CPM_TRANSFER_IDLE;
    return was_present;
}

void controller_profile_registry_apply_rescan(
    controller_profile_registry_t *registry,
    const controller_profile_registry_t *scanned)
{
    if (!registry || !scanned) {
        return;
    }
    bool present = registry->controller_present;
    uint16_t vid = registry->connected_vid;
    uint16_t pid = registry->connected_pid;
    uint16_t caps = registry->connected_caps;
    char product[CPM_PRODUCT_MAX + 1];
    memcpy(product, registry->connected_product, sizeof(product));

    *registry = *scanned;
    registry->controller_present = present;
    registry->connected_vid = vid;
    registry->connected_pid = pid;
    registry->connected_caps = caps;
    memcpy(registry->connected_product, product,
           sizeof(registry->connected_product));
    if (present) {
        (void)controller_profile_registry_on_descriptor(registry, vid, pid);
    }
}

void controller_profile_registry_mark_transfer_started(controller_profile_registry_t *reg,
                                                       int index)
{
    if (!reg || index < 0 || index >= (int)reg->count) {
        return;
    }
    reg->matched_index = (int8_t)index;
    reg->active_index = -1;
    reg->transfer_state = CPM_TRANSFER_TRANSFERRING;
}

void controller_profile_registry_mark_transfer_active(controller_profile_registry_t *reg,
                                                     int index)
{
    if (!reg || index < 0 || index >= (int)reg->count) {
        return;
    }
    reg->matched_index = (int8_t)index;
    reg->active_index = (int8_t)index;
    reg->transfer_state = CPM_TRANSFER_ACTIVE;
}

void controller_profile_registry_mark_transfer_failed(controller_profile_registry_t *reg,
                                                     int index)
{
    if (!reg || index < 0 || index >= (int)reg->count) {
        return;
    }
    reg->matched_index = (int8_t)index;
    reg->active_index = -1;
    reg->transfer_state = CPM_TRANSFER_FAILED;
}

/* ── Firmware glue (ESP-IDF only) ──────────────────────────────────────────── */

#ifndef CONTROLLER_PROFILE_MANAGER_PC_TEST

#include "control_link.h"
#include "service_log.h"
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
static SemaphoreHandle_t s_manager_mutex;
static uint16_t s_log_vid = 0xFFFFu;
static uint16_t s_log_pid = 0xFFFFu;
static uint16_t s_log_caps = 0xFFFFu;
static int s_log_idx = -2;

/* Profile-transfer sender: runs off the control-link RX task so the multi-KB
 * stream to the S3 never blocks event/descriptor handling. */
static QueueHandle_t s_send_q;
static SemaphoreHandle_t s_reply_sem;
static volatile bool s_reply_ack;
static volatile uint8_t s_reply_ref;
static volatile uint8_t s_reply_reason;

/* Dedup: the S3 re-announces the connected controller every heartbeat (~5 s) so
 * a freshly-booted P4 can (re)learn it. Without this guard the P4 would re-stream
 * and re-ACTIVATE the whole (up to 16 KB) profile on every announcement — that
 * floods the control link and resets the S3's live runtime state mid-set.
 * Remember which VID/PID is already transferred+active and skip repeats; a
 * failed transfer clears the mark so the next announcement retries. The S3
 * keeps its runtime profile across a controller unplug/replug, so re-sending on
 * reconnect is unnecessary. */
static volatile bool s_transferred_valid;
static volatile uint16_t s_transferred_vid;
static volatile uint16_t s_transferred_pid;

static bool cpm_lock(void)
{
    return s_manager_mutex &&
           xSemaphoreTake(s_manager_mutex, portMAX_DELAY) == pdTRUE;
}

static void cpm_unlock(void)
{
    xSemaphoreGive(s_manager_mutex);
}

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
    if (!cpm_lock()) {
        return false;
    }
    FILE *f = fopen(m->path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s", m->path);
        cpm_unlock();
        return false;
    }
    uint8_t *buf = malloc(m->size);
    if (!buf) {
        fclose(f);
        cpm_unlock();
        return false;
    }
    size_t got = fread(buf, 1, m->size, f);
    fclose(f);
    cpm_unlock();
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
        if (!cpm_lock()) {
            continue;
        }
        if (idx < 0 || idx >= (int)s_registry.count ||
            !s_registry.controller_present ||
            s_registry.matched_index != idx ||
            s_registry.transfer_state != CPM_TRANSFER_TRANSFERRING) {
            cpm_unlock();
            continue;
        }
        controller_profile_meta_t m = s_registry.profiles[idx];
        cpm_unlock();
        if (!m.valid) {
            continue;
        }
        bool ok = cpm_stream_profile(&m);
        if (!cpm_lock()) {
            continue;
        }
        bool still_connected = s_registry.controller_present &&
                               s_registry.connected_vid == m.vid &&
                               s_registry.connected_pid == m.pid &&
                               s_registry.matched_index == idx;
        if (ok && still_connected) {
            s_transferred_vid = m.vid;
            s_transferred_pid = m.pid;
            s_transferred_valid = true;
            controller_profile_registry_mark_transfer_active(&s_registry, idx);
        } else if (still_connected) {
            s_transferred_valid = false;   /* retry on the next announcement */
            controller_profile_registry_mark_transfer_failed(&s_registry, idx);
        } else {
            ok = false;
            s_transferred_valid = false;
        }
        cpm_unlock();
        ESP_LOGI(TAG, "profile '%s' transfer to S3 %s", m.id,
                 ok ? "OK" : "FAILED");
        service_log_event(ok ? SERVICE_LOG_PROFILE_TRANSFER_DONE
                             : SERVICE_LOG_PROFILE_TRANSFER_FAILED,
                          ok ? SERVICE_LOG_INFO : SERVICE_LOG_WARN,
                          2u, m.vid, m.pid, 0u, 0u, m.id);
    }
}

esp_err_t controller_profile_manager_init(void)
{
    if (!s_manager_mutex) {
        s_manager_mutex = xSemaphoreCreateMutex();
        if (!s_manager_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!cpm_lock()) {
        return ESP_FAIL;
    }
    memset(&s_registry, 0, sizeof(s_registry));
    s_registry.matched_index = -1;
    s_registry.active_index = -1;
    s_registry.transfer_state = CPM_TRANSFER_IDLE;
    s_transferred_valid = false;
    s_log_vid = 0xFFFFu;
    s_log_pid = 0xFFFFu;
    s_log_caps = 0xFFFFu;
    s_log_idx = -2;
    cpm_unlock();

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
    controller_profile_registry_t *scanned = calloc(1, sizeof(*scanned));
    if (!scanned) {
        return ESP_ERR_NO_MEM;
    }
    if (!cpm_lock()) {
        free(scanned);
        return ESP_FAIL;
    }
    if (s_registry.transfer_state == CPM_TRANSFER_TRANSFERRING) {
        cpm_unlock();
        free(scanned);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = controller_profile_scan_dir(CONFIG_CONTROLLER_PROFILE_SD_PATH,
                                               scanned);
    if (rc == ESP_ERR_NOT_FOUND) {
        cpm_unlock();
        free(scanned);
        ESP_LOGW(TAG, "no %s directory (SD missing or no profiles)",
                 CONFIG_CONTROLLER_PROFILE_SD_PATH);
        return rc;
    }
    if (rc != ESP_OK) {
        cpm_unlock();
        free(scanned);
        return rc;
    }

    controller_profile_registry_apply_rescan(&s_registry, scanned);
    *scanned = s_registry;
    s_transferred_valid = false;
    cpm_unlock();

    ESP_LOGI(TAG, "%u controller profile(s) in %s",
             (unsigned)scanned->count, CONFIG_CONTROLLER_PROFILE_SD_PATH);
    for (uint8_t i = 0; i < scanned->count; i++) {
        const controller_profile_meta_t *m = &scanned->profiles[i];
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
    free(scanned);
    return ESP_OK;
}

esp_err_t controller_profile_manager_install_profile(
    const char *id, const uint8_t *data, size_t len, bool overwrite,
    controller_profile_meta_t *out_meta)
{
    controller_profile_registry_t *scanned = calloc(1, sizeof(*scanned));
    if (!scanned) {
        return ESP_ERR_NO_MEM;
    }
    if (!cpm_lock()) {
        free(scanned);
        return ESP_FAIL;
    }
    if (s_registry.transfer_state == CPM_TRANSFER_TRANSFERRING ||
        (s_send_q && uxQueueMessagesWaiting(s_send_q) > 0)) {
        cpm_unlock();
        free(scanned);
        return ESP_ERR_INVALID_STATE;
    }

    controller_profile_meta_t installed;
    esp_err_t rc = controller_profile_storage_install(
        CONFIG_CONTROLLER_PROFILE_SD_PATH, id, data, len, overwrite,
        &installed);
    if (rc != ESP_OK) {
        cpm_unlock();
        free(scanned);
        return rc;
    }

    rc = controller_profile_scan_dir(CONFIG_CONTROLLER_PROFILE_SD_PATH,
                                     scanned);
    if (rc != ESP_OK) {
        cpm_unlock();
        free(scanned);
        return rc;
    }
    controller_profile_registry_apply_rescan(&s_registry, scanned);
    bool reactivate = s_registry.controller_present;
    uint16_t vid = s_registry.connected_vid;
    uint16_t pid = s_registry.connected_pid;
    uint16_t caps = s_registry.connected_caps;
    char product[CPM_PRODUCT_MAX + 1];
    memcpy(product, s_registry.connected_product, sizeof(product));
    s_transferred_valid = false;
    if (out_meta) {
        *out_meta = installed;
    }
    cpm_unlock();
    free(scanned);

    ESP_LOGI(TAG, "profile '%s' installed (%u B), registry rescanned", id,
             (unsigned)installed.size);
    if (reactivate) {
        (void)controller_profile_manager_on_descriptor_report(
            vid, pid, caps, product);
    }
    return ESP_OK;
}

esp_err_t controller_profile_manager_get_registry_snapshot(
    controller_profile_registry_t *out_registry)
{
    if (!out_registry) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cpm_lock()) {
        return ESP_FAIL;
    }
    *out_registry = s_registry;
    cpm_unlock();
    return ESP_OK;
}

static int cpm_on_descriptor_locked(uint16_t vid, uint16_t pid)
{
    if (s_registry.controller_present &&
        s_registry.connected_vid == vid && s_registry.connected_pid == pid &&
        s_registry.transfer_state == CPM_TRANSFER_TRANSFERRING) {
        /* The S3 repeats its descriptor heartbeat. Do not reset MATCHED state
         * or enqueue a second copy while the sender owns this profile. */
        return controller_profile_registry_match(&s_registry, vid, pid);
    }
    int idx = controller_profile_registry_on_descriptor(&s_registry, vid, pid);
    if (idx >= 0) {
        if (s_transferred_valid && s_transferred_vid == vid && s_transferred_pid == pid) {
            /* Already streamed + activated for this controller; the S3 still
             * holds it. Skip the redundant re-transfer (see dedup note above). */
            controller_profile_registry_mark_transfer_active(&s_registry, idx);
            ESP_LOGD(TAG, "controller VID=0x%04X PID=0x%04X profile '%s' already active",
                     vid, pid, s_registry.profiles[idx].id);
            return idx;
        }
        ESP_LOGI(TAG, "controller VID=0x%04X PID=0x%04X -> profile '%s'",
                 vid, pid, s_registry.profiles[idx].id);
        /* Hand the transfer to the sender task; drop silently if it is busy
         * (the next connect/heartbeat descriptor re-triggers a match). */
        if (s_send_q) {
            if (xQueueSend(s_send_q, &idx, 0) == pdTRUE) {
                controller_profile_registry_mark_transfer_started(&s_registry, idx);
            }
        }
    } else {
        /* No profile for this controller — forget any previous mark so a later
         * re-match streams fresh. */
        s_transferred_valid = false;
        ESP_LOGW(TAG, "controller VID=0x%04X PID=0x%04X has no profile", vid, pid);
    }
    return idx;
}

int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid)
{
    if (!cpm_lock()) {
        return -1;
    }
    int idx = cpm_on_descriptor_locked(vid, pid);
    cpm_unlock();
    return idx;
}

int controller_profile_manager_on_descriptor_report(uint16_t vid, uint16_t pid,
                                                    uint16_t caps,
                                                    const char *product)
{
    if (!cpm_lock()) {
        return -1;
    }
    s_registry.connected_caps = caps;
    memset(s_registry.connected_product, 0, sizeof(s_registry.connected_product));
    if (product) {
        snprintf(s_registry.connected_product, sizeof(s_registry.connected_product),
                 "%.*s", CPM_PRODUCT_MAX, product);
    }
    ESP_LOGI(TAG, "connected controller '%s' caps=0x%04X",
             s_registry.connected_product, caps);
    int idx = cpm_on_descriptor_locked(vid, pid);
    char product_copy[CPM_PRODUCT_MAX + 1];
    snprintf(product_copy, sizeof(product_copy), "%s", s_registry.connected_product);
    cpm_unlock();

    /* The S3 re-announces the descriptor on every heartbeat, so only log the
     * edges — otherwise the journal fills with identical entries. */
    if (vid != s_log_vid || pid != s_log_pid || caps != s_log_caps ||
        idx != s_log_idx) {
        service_log_event(SERVICE_LOG_CONTROLLER_CONNECTED, SERVICE_LOG_INFO,
                          3u, vid, pid, caps, 0u, product_copy);
        /* A successful match is already implied by the CONTROLLER_CONNECTED
         * record immediately above and named by the PROFILE_TRANSFER_DONE that
         * follows, so recording it again only costs a microSD write on a card
         * the recorder is also writing to. Log the failure, which nothing else
         * reports. */
        if (idx < 0) {
            service_log_event(SERVICE_LOG_PROFILE_MATCHED, SERVICE_LOG_WARN,
                              2u, vid, pid, 0u, 0u, "unsupported");
        }
        s_log_vid = vid;
        s_log_pid = pid;
        s_log_caps = caps;
        s_log_idx = idx;
    }
    return idx;
}

bool controller_profile_manager_on_disconnect(void)
{
    if (!cpm_lock()) {
        return false;
    }
    uint16_t vid = s_registry.connected_vid;
    uint16_t pid = s_registry.connected_pid;
    char product[CPM_PRODUCT_MAX + 1];
    snprintf(product, sizeof(product), "%s", s_registry.connected_product);
    bool changed = controller_profile_registry_on_disconnect(&s_registry);
    s_transferred_valid = false;
    s_log_vid = 0xFFFFu;
    s_log_pid = 0xFFFFu;
    s_log_caps = 0xFFFFu;
    s_log_idx = -2;
    cpm_unlock();

    if (changed) {
        service_log_event(SERVICE_LOG_CONTROLLER_DISCONNECTED, SERVICE_LOG_WARN,
                          2u, vid, pid, 0u, 0u, product);
        ESP_LOGW(TAG, "controller disconnected VID=0x%04X PID=0x%04X '%s'",
                 vid, pid, product);
    }
    return changed;
}

#endif /* CONTROLLER_PROFILE_MANAGER_PC_TEST */
