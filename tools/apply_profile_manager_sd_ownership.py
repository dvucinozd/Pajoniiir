#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
P = ROOT / "firmware/main-deck-p4/components/controller_profile_manager/controller_profile_manager.c"


def replace_once(s, old, new, label):
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected 1, got {n}")
    return s.replace(old, new, 1)


def replace_function(s, signature, replacement):
    start = s.find(signature)
    if start < 0:
        raise RuntimeError(f"missing {signature}")
    brace = s.find('{', start)
    depth = 0
    for i in range(brace, len(s)):
        if s[i] == '{': depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0:
                return s[:start] + replacement.rstrip() + s[i+1:]
    raise RuntimeError(f"unterminated {signature}")

s = P.read_text(encoding="utf-8")
s = replace_once(s, '#include "service_log.h"\n', '#include "service_log.h"\n#include "sd_io_gate.h"\n', 'sd gate include')
s = replace_once(s,
'''static QueueHandle_t s_send_q;
static SemaphoreHandle_t s_reply_sem;
''',
'''static QueueHandle_t s_send_q;
static QueueHandle_t s_descriptor_q;
static SemaphoreHandle_t s_reply_sem;
static volatile bool s_storage_busy;

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint16_t caps;
    char product[CPM_PRODUCT_MAX + 1];
} cpm_descriptor_report_t;
''', 'descriptor state')

s = replace_function(s, 'static bool cpm_stream_profile(const controller_profile_meta_t *m)', r'''static bool cpm_stream_profile(const controller_profile_meta_t *m)
{
    if (!m || !m->valid || m->size == 0u || m->size > CPM_MAX_PROFILE_SIZE) {
        return false;
    }

    uint8_t *buf = malloc(m->size);
    if (!buf) return false;

    /* Bounded SD read under the global SD owner. Never hold the manager mutex:
     * the UART RX descriptor callback must remain able to enqueue heartbeats. */
    sd_io_gate_begin();
    FILE *f = fopen(m->path, "rb");
    size_t got = 0u;
    if (f) {
        got = fread(buf, 1, m->size, f);
        fclose(f);
    }
    sd_io_gate_end();
    if (got != m->size) {
        ESP_LOGW(TAG, "cannot read complete profile %s", m->path);
        free(buf);
        return false;
    }

    uint32_t crc = cp_xfer_crc32(buf, m->size);
    bool ok = false;
    for (int attempt = 1; attempt <= CPM_SEND_ATTEMPTS && !ok; attempt++) {
        while (xSemaphoreTake(s_reply_sem, 0) == pdTRUE) {}
        if (control_link_send_profile_begin((uint32_t)m->size, crc,
                                            m->vid, m->pid) != ESP_OK ||
            !cpm_wait_reply(CTRL_BULK_TYPE_PROFILE_BEGIN)) {
            continue;
        }
        bool chunks_ok = true;
        for (uint32_t off = 0; off < m->size; off += CTRL_PROFILE_CHUNK_MAX) {
            size_t n = m->size - off;
            if (n > CTRL_PROFILE_CHUNK_MAX) n = CTRL_PROFILE_CHUNK_MAX;
            if (control_link_send_profile_chunk(off, buf + off, n) != ESP_OK) {
                chunks_ok = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(CPM_CHUNK_PACE_MS));
        }
        if (!chunks_ok) continue;
        if (control_link_send_profile_simple(CTRL_BULK_TYPE_PROFILE_END) != ESP_OK ||
            !cpm_wait_reply(CTRL_BULK_TYPE_PROFILE_END)) continue;
        if (control_link_send_profile_simple(CTRL_BULK_TYPE_PROFILE_ACTIVATE) != ESP_OK ||
            !cpm_wait_reply(CTRL_BULK_TYPE_PROFILE_ACTIVATE)) continue;
        ok = true;
    }
    free(buf);
    return ok;
}''')

# Insert descriptor worker before init.
anchor = 'esp_err_t controller_profile_manager_init(void)\n'
worker = r'''static int cpm_process_descriptor_report(uint16_t vid, uint16_t pid,
                                         uint16_t caps, const char *product)
{
    if (!cpm_lock()) return -1;
    s_registry.connected_caps = caps;
    memset(s_registry.connected_product, 0, sizeof(s_registry.connected_product));
    if (product) {
        snprintf(s_registry.connected_product, sizeof(s_registry.connected_product),
                 "%.*s", CPM_PRODUCT_MAX, product);
    }
    int idx = cpm_on_descriptor_locked(vid, pid);
    char product_copy[CPM_PRODUCT_MAX + 1];
    snprintf(product_copy, sizeof(product_copy), "%s", s_registry.connected_product);
    cpm_unlock();

    if (vid != s_log_vid || pid != s_log_pid || caps != s_log_caps || idx != s_log_idx) {
        service_log_event(SERVICE_LOG_CONTROLLER_CONNECTED, SERVICE_LOG_INFO,
                          3u, vid, pid, caps, 0u, product_copy);
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

static void cpm_descriptor_task(void *arg)
{
    (void)arg;
    cpm_descriptor_report_t report;
    for (;;) {
        if (xQueueReceive(s_descriptor_q, &report, portMAX_DELAY) != pdTRUE) continue;
        while (__atomic_load_n(&s_storage_busy, __ATOMIC_ACQUIRE)) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        (void)cpm_process_descriptor_report(report.vid, report.pid,
                                            report.caps, report.product);
    }
}

'''
if anchor not in s: raise RuntimeError('init anchor missing')
s = s.replace(anchor, worker + anchor, 1)

# Extend init queue/task setup.
old = '''        s_reply_sem = xSemaphoreCreateBinary();
        s_send_q = xQueueCreate(2, sizeof(int));
        if (!s_reply_sem || !s_send_q) {
            return ESP_ERR_NO_MEM;
        }
        if (xTaskCreate(cpm_sender_task, "cpm_send", CPM_SENDER_STACK, NULL, 4,
                        NULL) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
        control_link_set_profile_reply_cb(cpm_on_reply);
'''
new = '''        s_reply_sem = xSemaphoreCreateBinary();
        s_send_q = xQueueCreate(2, sizeof(int));
        s_descriptor_q = xQueueCreate(8, sizeof(cpm_descriptor_report_t));
        if (!s_reply_sem || !s_send_q || !s_descriptor_q) {
            return ESP_ERR_NO_MEM;
        }
        if (xTaskCreate(cpm_sender_task, "cpm_send", CPM_SENDER_STACK, NULL, 4,
                        NULL) != pdPASS ||
            xTaskCreate(cpm_descriptor_task, "cpm_desc", 3072, NULL, 5,
                        NULL) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
        control_link_set_profile_reply_cb(cpm_on_reply);
'''
s = replace_once(s, old, new, 'init queues')

s = replace_function(s, 'esp_err_t controller_profile_manager_scan_storage(void)', r'''esp_err_t controller_profile_manager_scan_storage(void)
{
    controller_profile_registry_t *scanned = calloc(1, sizeof(*scanned));
    if (!scanned) return ESP_ERR_NO_MEM;

    if (!cpm_lock()) { free(scanned); return ESP_FAIL; }
    if (s_registry.transfer_state == CPM_TRANSFER_TRANSFERRING || s_storage_busy) {
        cpm_unlock(); free(scanned); return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&s_storage_busy, true, __ATOMIC_RELEASE);
    cpm_unlock();

    sd_io_gate_begin();
    esp_err_t rc = controller_profile_scan_dir(CONFIG_CONTROLLER_PROFILE_SD_PATH, scanned);
    sd_io_gate_end();

    if (rc == ESP_OK && cpm_lock()) {
        controller_profile_registry_apply_rescan(&s_registry, scanned);
        *scanned = s_registry;
        s_transferred_valid = false;
        cpm_unlock();
    }
    __atomic_store_n(&s_storage_busy, false, __ATOMIC_RELEASE);

    if (rc == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no %s directory (SD missing or no profiles)",
                 CONFIG_CONTROLLER_PROFILE_SD_PATH);
    } else if (rc == ESP_OK) {
        ESP_LOGI(TAG, "%u controller profile(s) in %s",
                 (unsigned)scanned->count, CONFIG_CONTROLLER_PROFILE_SD_PATH);
    }
    free(scanned);
    return rc;
}''')

s = replace_function(s, 'esp_err_t controller_profile_manager_install_profile(', r'''esp_err_t controller_profile_manager_install_profile(
    const char *id, const uint8_t *data, size_t len, bool overwrite,
    controller_profile_meta_t *out_meta)
{
    controller_profile_registry_t *scanned = calloc(1, sizeof(*scanned));
    if (!scanned) return ESP_ERR_NO_MEM;

    if (!cpm_lock()) { free(scanned); return ESP_FAIL; }
    if (s_registry.transfer_state == CPM_TRANSFER_TRANSFERRING ||
        (s_send_q && uxQueueMessagesWaiting(s_send_q) > 0) || s_storage_busy) {
        cpm_unlock(); free(scanned); return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&s_storage_busy, true, __ATOMIC_RELEASE);
    cpm_unlock();

    controller_profile_meta_t installed = {0};
    sd_io_gate_begin();
    /* Admission can change while waiting for the gate. Never install beside an
     * active recording session; retry after recording stops. */
    esp_err_t rc = sd_io_gate_recorder_active()
                       ? ESP_ERR_INVALID_STATE
                       : controller_profile_storage_install(
                             CONFIG_CONTROLLER_PROFILE_SD_PATH, id, data, len,
                             overwrite, &installed);
    if (rc == ESP_OK) {
        rc = controller_profile_scan_dir(CONFIG_CONTROLLER_PROFILE_SD_PATH, scanned);
    }
    sd_io_gate_end();

    bool reactivate = false;
    uint16_t vid = 0, pid = 0, caps = 0;
    char product[CPM_PRODUCT_MAX + 1] = {0};
    if (rc == ESP_OK && cpm_lock()) {
        controller_profile_registry_apply_rescan(&s_registry, scanned);
        reactivate = s_registry.controller_present;
        vid = s_registry.connected_vid;
        pid = s_registry.connected_pid;
        caps = s_registry.connected_caps;
        memcpy(product, s_registry.connected_product, sizeof(product));
        s_transferred_valid = false;
        if (out_meta) *out_meta = installed;
        cpm_unlock();
    }
    __atomic_store_n(&s_storage_busy, false, __ATOMIC_RELEASE);
    free(scanned);

    if (rc == ESP_OK) {
        ESP_LOGI(TAG, "profile '%s' installed (%u B), registry rescanned",
                 id, (unsigned)installed.size);
        if (reactivate) {
            cpm_descriptor_report_t report = {
                .vid = vid, .pid = pid, .caps = caps,
            };
            snprintf(report.product, sizeof(report.product), "%s", product);
            (void)xQueueSend(s_descriptor_q, &report, 0);
        }
    }
    return rc;
}''')

# Replace both public descriptor entry points; worker owns registry mutation.
s = replace_function(s, 'int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid)', r'''int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid)
{
    return controller_profile_manager_on_descriptor_report(vid, pid, 0u, NULL);
}''')
s = replace_function(s, 'int controller_profile_manager_on_descriptor_report(uint16_t vid, uint16_t pid,', r'''int controller_profile_manager_on_descriptor_report(uint16_t vid, uint16_t pid,
                                                     uint16_t caps,
                                                     const char *product)
{
    if (!s_descriptor_q) return -1;
    cpm_descriptor_report_t report = {
        .vid = vid, .pid = pid, .caps = caps,
    };
    if (product) {
        snprintf(report.product, sizeof(report.product), "%.*s",
                 CPM_PRODUCT_MAX, product);
    }
    /* UART RX never waits for SD or the manager mutex. If all slots are busy,
     * replace the oldest heartbeat with the newest descriptor state. */
    if (xQueueSend(s_descriptor_q, &report, 0) != pdTRUE) {
        cpm_descriptor_report_t stale;
        (void)xQueueReceive(s_descriptor_q, &stale, 0);
        if (xQueueSend(s_descriptor_q, &report, 0) != pdTRUE) return -1;
    }
    return 0;
}''')

P.write_text(s, encoding='utf-8')
print('profile manager ownership patch applied')
