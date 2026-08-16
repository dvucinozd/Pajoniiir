#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

/* Referenced only by ESP_LOG*, which the PC host stubs compile away. */
__attribute__((unused)) static const char *TAG = "settings";
#define NS  "cdjcfg"
#define APP_SETTINGS_SCHEMA_KEY       "schema_ver"
#define APP_SETTINGS_SCHEMA_VERSION   1u

#if defined(CONFIG_BSP_PCM5102A_MAIN_OUT) && CONFIG_BSP_PCM5102A_MAIN_OUT && \
    (!defined(CONFIG_BSP_ES8311_MONITOR) || !CONFIG_BSP_ES8311_MONITOR)
#define APP_SETTINGS_SPEAKER_ROUTE_RETIRED 1
#else
#define APP_SETTINGS_SPEAKER_ROUTE_RETIRED 0
#endif

/* Readers run in UI/http/OTA tasks. Publish coherent RAM snapshots only after
 * durable NVS writes have succeeded. */
static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;

/* Defaults match the firmware's out-of-the-box behaviour. */
#define APP_SETTINGS_DEFAULTS (app_settings_t){ \
    .audio_out     = APP_SETTINGS_AUDIO_OUT_RCA, \
    .backlight_pct = 80,                         \
    .time_remain   = 0,                          \
    .cue_mode      = 0,                          \
    .master_trim_preset = 0,                     \
    .wifi_remote   = 0,                          \
}

static app_settings_t s_cfg = {
    .audio_out     = APP_SETTINGS_AUDIO_OUT_RCA,
    .backlight_pct = 80,
    .time_remain   = 0,
    .cue_mode      = 0,
    .master_trim_preset = 0,
    .wifi_remote   = 0,
};

static char s_ota_ssid[APP_SETTINGS_OTA_SSID_CAP];
static char s_ota_pass[APP_SETTINGS_OTA_PASS_CAP];
static char s_ota_url[APP_SETTINGS_OTA_URL_CAP];

static size_t bounded_strlen(const char *src, size_t limit)
{
    size_t len = 0u;
    if (!src) return 0u;
    while (len < limit && src[len] != '\0') len++;
    return len;
}

static void copy_bounded(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0u) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = bounded_strlen(src, cap - 1u);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static esp_err_t save_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t rc = nvs_open(NS, NVS_READWRITE, &handle);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(rw) failed for %s: %s", key, esp_err_to_name(rc));
        return rc;
    }
    rc = nvs_set_u8(handle, key, value);
    if (rc == ESP_OK) rc = nvs_commit(handle);
    nvs_close(handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist %s: %s", key, esp_err_to_name(rc));
    }
    return rc;
}

static esp_err_t migrate_settings(nvs_handle_t handle,
                                  app_settings_t *settings,
                                  uint8_t stored_schema,
                                  bool schema_found)
{
    bool changed = false;

    if (settings->audio_out > APP_SETTINGS_AUDIO_OUT_RCA) {
        settings->audio_out = APP_SETTINGS_AUDIO_OUT_RCA;
        changed = true;
    }

#if APP_SETTINGS_SPEAKER_ROUTE_RETIRED
    if (settings->audio_out == APP_SETTINGS_AUDIO_OUT_SPEAKER) {
        settings->audio_out = APP_SETTINGS_AUDIO_OUT_RCA;
        changed = true;
    }
#endif

    if (schema_found && stored_schema >= APP_SETTINGS_SCHEMA_VERSION && !changed) {
        return ESP_OK;
    }

    esp_err_t rc = nvs_set_u8(handle, "audio_out", settings->audio_out);
    if (rc == ESP_OK) {
        rc = nvs_set_u8(handle, APP_SETTINGS_SCHEMA_KEY, APP_SETTINGS_SCHEMA_VERSION);
    }
    if (rc == ESP_OK) {
        rc = nvs_commit(handle);
    }
    if (rc == ESP_OK) {
        ESP_LOGW(TAG, "settings schema migrated to v%u; audio output forced to safe route",
                 (unsigned)APP_SETTINGS_SCHEMA_VERSION);
    } else {
        ESP_LOGE(TAG, "settings migration failed: %s", esp_err_to_name(rc));
    }
    return rc;
}

static void load_ota_config(nvs_handle_t handle,
                            char ssid[APP_SETTINGS_OTA_SSID_CAP],
                            char pass[APP_SETTINGS_OTA_PASS_CAP],
                            char url[APP_SETTINGS_OTA_URL_CAP])
{
    size_t len = APP_SETTINGS_OTA_SSID_CAP;
    if (nvs_get_str(handle, "ota_ssid", ssid, &len) != ESP_OK) ssid[0] = '\0';
    len = APP_SETTINGS_OTA_PASS_CAP;
    if (nvs_get_str(handle, "ota_pass", pass, &len) != ESP_OK) pass[0] = '\0';
    len = APP_SETTINGS_OTA_URL_CAP;
    if (nvs_get_str(handle, "ota_url", url, &len) != ESP_OK) url[0] = '\0';
}

/* Defined with the rest of the backlight debounce path, below the generated
 * setters it has to sit after; called from app_settings_init() above it. */
esp_err_t app_settings_start_backlight_worker(void);

esp_err_t app_settings_init(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s) — erasing", esp_err_to_name(rc));
        rc = nvs_flash_erase();
        if (rc == ESP_OK) rc = nvs_flash_init();
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(rc));
        return rc;
    }

    app_settings_t next = s_cfg;
    char ota_ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char ota_pass[APP_SETTINGS_OTA_PASS_CAP] = {0};
    char ota_url[APP_SETTINGS_OTA_URL_CAP] = {0};

    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READWRITE, &handle) == ESP_OK) {
        uint8_t value;
        if (nvs_get_u8(handle, "audio_out", &value) == ESP_OK) next.audio_out = value;
        if (nvs_get_u8(handle, "backlight", &value) == ESP_OK) next.backlight_pct = value > 100 ? 100 : value;
        if (nvs_get_u8(handle, "time_rem", &value) == ESP_OK) next.time_remain = value;
        if (nvs_get_u8(handle, "cue_mode", &value) == ESP_OK && value <= 1) next.cue_mode = value;
        if (nvs_get_u8(handle, "master_trim", &value) == ESP_OK && value <= 2) next.master_trim_preset = value;
        if (nvs_get_u8(handle, "wifi_rem", &value) == ESP_OK && value <= 1) next.wifi_remote = value;
        load_ota_config(handle, ota_ssid, ota_pass, ota_url);

        uint8_t stored_schema = 0u;
        bool schema_found = nvs_get_u8(handle, APP_SETTINGS_SCHEMA_KEY, &stored_schema) == ESP_OK;
        (void)migrate_settings(handle, &next, stored_schema, schema_found);
        nvs_close(handle);
    }

    portENTER_CRITICAL(&s_cfg_mux);
    s_cfg = next;
    memcpy(s_ota_ssid, ota_ssid, sizeof(s_ota_ssid));
    memcpy(s_ota_pass, ota_pass, sizeof(s_ota_pass));
    memcpy(s_ota_url, ota_url, sizeof(s_ota_url));
    portEXIT_CRITICAL(&s_cfg_mux);

    ESP_LOGI(TAG, "loaded: audio_out=%s backlight=%u time_remain=%u cue_mode=%u master_trim=%u wifi_remote=%s",
             next.audio_out == APP_SETTINGS_AUDIO_OUT_RCA ? "rca" : "speaker",
             next.backlight_pct, next.time_remain,
             next.cue_mode, next.master_trim_preset,
             next.wifi_remote ? "on" : "off");

    return app_settings_start_backlight_worker();
}

app_settings_t app_settings_get(void)
{
    app_settings_t snapshot;
    portENTER_CRITICAL(&s_cfg_mux);
    snapshot = s_cfg;
    portEXIT_CRITICAL(&s_cfg_mux);
    return snapshot;
}

#define DEFINE_U8_SETTER(function_name, field_name, key_name, normalize_stmt) \
void function_name(uint8_t value)                                             \
{                                                                              \
    normalize_stmt;                                                            \
    app_settings_t current = app_settings_get();                               \
    if (current.field_name == value) return;                                   \
    if (save_u8(key_name, value) != ESP_OK) return;                            \
    portENTER_CRITICAL(&s_cfg_mux);                                            \
    s_cfg.field_name = value;                                                  \
    portEXIT_CRITICAL(&s_cfg_mux);                                             \
}

#if APP_SETTINGS_SPEAKER_ROUTE_RETIRED
DEFINE_U8_SETTER(app_settings_set_audio_out, audio_out, "audio_out", value = APP_SETTINGS_AUDIO_OUT_RCA)
#else
DEFINE_U8_SETTER(app_settings_set_audio_out, audio_out, "audio_out", if (value > APP_SETTINGS_AUDIO_OUT_RCA) value = APP_SETTINGS_AUDIO_OUT_RCA)
#endif
DEFINE_U8_SETTER(app_settings_set_time_remain, time_remain, "time_rem", (void)0)
DEFINE_U8_SETTER(app_settings_set_cue_mode, cue_mode, "cue_mode", if (value > 1) value = 0)
DEFINE_U8_SETTER(app_settings_set_master_trim_preset, master_trim_preset, "master_trim", if (value > 2) value = 0)
DEFINE_U8_SETTER(app_settings_set_wifi_remote, wifi_remote, "wifi_rem", value = value ? 1 : 0)

/* ── Backlight: live now, persisted once the slider settles ──────────────── *
 *
 * Backlight is the one setting driven by a continuous control. Committing every
 * VALUE_CHANGED would put an NVS write per slider sample on the LVGL task, so
 * the value is published immediately for the live backlight and only written
 * after the control has been quiet for BACKLIGHT_DEBOUNCE_MS, on a worker.
 *
 * This used to live in a wrapper translation unit that renamed the generated
 * setter to *_legacy_immediate and #included this file; the setter is simply
 * written out by hand here instead, and the generated one is no longer produced.
 */
#define BACKLIGHT_DEBOUNCE_MS 500u
#define SETTINGS_WORKER_STACK 3072u
#define SETTINGS_WORKER_PRIO  2u

static TaskHandle_t s_settings_worker;
static uint8_t s_pending_backlight = 80u;

/* Synchronous commit, used before the worker exists and as its final step.
 * Returns true when the value actually reached NVS and was published. */
static bool app_settings_commit_backlight(uint8_t value)
{
    if (value > 100u) value = 100u;
    app_settings_t current = app_settings_get();
    if (current.backlight_pct == value) return false;
    if (save_u8("backlight", value) != ESP_OK) return false;
    portENTER_CRITICAL(&s_cfg_mux);
    s_cfg.backlight_pct = value;
    portEXIT_CRITICAL(&s_cfg_mux);
    return true;
}

/* One debounce cycle: block for a change, absorb the rest of the burst, commit
 * whatever the control settled on. Kept as its own function so a host test can
 * drive exactly one cycle; the worker loop below is an infinite loop and cannot
 * be entered directly by a test without a scheduler to leave it. */
static void settings_persist_debounce_cycle(void)
{
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Each new VALUE_CHANGED notification restarts the quiet interval. */
    while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BACKLIGHT_DEBOUNCE_MS)) != 0u) {
    }

    const uint8_t value = __atomic_load_n(&s_pending_backlight, __ATOMIC_ACQUIRE);
    if (app_settings_commit_backlight(value)) {
        ESP_LOGI(TAG, "backlight persisted after debounce: %u%%", (unsigned)value);
    }
}

#if defined(APP_SETTINGS_HOST_TEST)
/* Test seam: run one cycle on the caller's stack instead of the worker task. */
void app_settings_test_run_debounce_cycle(void)
{
    settings_persist_debounce_cycle();
}

/* Return this translation unit to its power-on state. The host harness can reset
 * its fake RTOS between cases, but not the statics in here — without this a
 * worker created by one case makes the next one think it already has one. */
void app_settings_test_reset(void)
{
    s_settings_worker = NULL;
    s_pending_backlight = 80u;
    portENTER_CRITICAL(&s_cfg_mux);
    s_cfg = APP_SETTINGS_DEFAULTS;
    memset(s_ota_ssid, 0, sizeof(s_ota_ssid));
    memset(s_ota_pass, 0, sizeof(s_ota_pass));
    memset(s_ota_url, 0, sizeof(s_ota_url));
    portEXIT_CRITICAL(&s_cfg_mux);
}
#endif

static void settings_persist_worker(void *arg)
{
    (void)arg;
    for (;;) {
        settings_persist_debounce_cycle();
    }
}

void app_settings_set_backlight(uint8_t value)
{
    if (value > 100u) value = 100u;
    __atomic_store_n(&s_pending_backlight, value, __ATOMIC_RELEASE);

    /* Calls before app_settings_init() — tests, or an unusually early caller —
     * still have to persist, so fall back to the synchronous commit. */
    TaskHandle_t worker = __atomic_load_n(&s_settings_worker, __ATOMIC_ACQUIRE);
    if (!worker) {
        app_settings_commit_backlight(value);
        return;
    }
    xTaskNotifyGive(worker);
}

/* Start the debounce worker. Separate from the NVS load so the load stays
 * testable on its own, and so a worker failure is reported distinctly. */
esp_err_t app_settings_start_backlight_worker(void)
{
    app_settings_t snapshot = app_settings_get();
    __atomic_store_n(&s_pending_backlight, snapshot.backlight_pct, __ATOMIC_RELEASE);
    if (s_settings_worker) return ESP_OK;

    TaskHandle_t worker = NULL;
    if (xTaskCreate(settings_persist_worker, "settings_nvs", SETTINGS_WORKER_STACK,
                    NULL, SETTINGS_WORKER_PRIO, &worker) != pdPASS) {
        ESP_LOGE(TAG, "failed to create settings persistence worker");
        return ESP_ERR_NO_MEM;
    }
    __atomic_store_n(&s_settings_worker, worker, __ATOMIC_RELEASE);
    return ESP_OK;
}

#undef DEFINE_U8_SETTER

void app_settings_ota_get_ssid(char *out, size_t cap)
{
    portENTER_CRITICAL(&s_cfg_mux);
    copy_bounded(out, cap, s_ota_ssid);
    portEXIT_CRITICAL(&s_cfg_mux);
}

void app_settings_ota_get_url(char *out, size_t cap)
{
    portENTER_CRITICAL(&s_cfg_mux);
    copy_bounded(out, cap, s_ota_url);
    portEXIT_CRITICAL(&s_cfg_mux);
}

bool app_settings_ota_has_password(void)
{
    bool has_password;
    portENTER_CRITICAL(&s_cfg_mux);
    has_password = s_ota_pass[0] != '\0';
    portEXIT_CRITICAL(&s_cfg_mux);
    return has_password;
}

void app_settings_ota_copy_password(char *out, size_t cap)
{
    portENTER_CRITICAL(&s_cfg_mux);
    copy_bounded(out, cap, s_ota_pass);
    portEXIT_CRITICAL(&s_cfg_mux);
}

esp_err_t app_settings_ota_set(const char *ssid, const char *password, const char *url)
{
    char next_ssid[APP_SETTINGS_OTA_SSID_CAP];
    char next_pass[APP_SETTINGS_OTA_PASS_CAP];
    char next_url[APP_SETTINGS_OTA_URL_CAP];

    portENTER_CRITICAL(&s_cfg_mux);
    memcpy(next_ssid, s_ota_ssid, sizeof(next_ssid));
    memcpy(next_pass, s_ota_pass, sizeof(next_pass));
    memcpy(next_url, s_ota_url, sizeof(next_url));
    portEXIT_CRITICAL(&s_cfg_mux);

    if (ssid) copy_bounded(next_ssid, sizeof(next_ssid), ssid);
    if (url) copy_bounded(next_url, sizeof(next_url), url);
    /* NULL preserves the existing password; an empty string clears it. */
    if (password) copy_bounded(next_pass, sizeof(next_pass), password);

    nvs_handle_t handle;
    esp_err_t rc = nvs_open(NS, NVS_READWRITE, &handle);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(rw) failed for ota config: %s", esp_err_to_name(rc));
        return rc;
    }
    rc = nvs_set_str(handle, "ota_ssid", next_ssid);
    if (rc == ESP_OK) rc = nvs_set_str(handle, "ota_pass", next_pass);
    if (rc == ESP_OK) rc = nvs_set_str(handle, "ota_url", next_url);
    if (rc == ESP_OK) rc = nvs_commit(handle);
    nvs_close(handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "ota config persistence failed: %s", esp_err_to_name(rc));
        return rc;
    }

    portENTER_CRITICAL(&s_cfg_mux);
    memcpy(s_ota_ssid, next_ssid, sizeof(s_ota_ssid));
    memcpy(s_ota_pass, next_pass, sizeof(s_ota_pass));
    memcpy(s_ota_url, next_url, sizeof(s_ota_url));
    portEXIT_CRITICAL(&s_cfg_mux);

    ESP_LOGI(TAG, "ota config saved: ssid=\"%s\" url=\"%s\" password=%s",
             next_ssid, next_url, next_pass[0] ? "set" : "none");
    return ESP_OK;
}

void app_settings_ota_clear(void)
{
    nvs_handle_t handle;
    esp_err_t rc = nvs_open(NS, NVS_READWRITE, &handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "ota config clear open failed: %s", esp_err_to_name(rc));
        return;
    }
    rc = nvs_erase_key(handle, "ota_ssid");
    if (rc == ESP_ERR_NVS_NOT_FOUND) rc = ESP_OK;
    esp_err_t pass_rc = nvs_erase_key(handle, "ota_pass");
    if (pass_rc == ESP_ERR_NVS_NOT_FOUND) pass_rc = ESP_OK;
    esp_err_t url_rc = nvs_erase_key(handle, "ota_url");
    if (url_rc == ESP_ERR_NVS_NOT_FOUND) url_rc = ESP_OK;
    if (rc == ESP_OK) rc = pass_rc;
    if (rc == ESP_OK) rc = url_rc;
    if (rc == ESP_OK) rc = nvs_commit(handle);
    nvs_close(handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "ota config clear failed: %s", esp_err_to_name(rc));
        return;
    }

    portENTER_CRITICAL(&s_cfg_mux);
    memset(s_ota_ssid, 0, sizeof(s_ota_ssid));
    memset(s_ota_pass, 0, sizeof(s_ota_pass));
    memset(s_ota_url, 0, sizeof(s_ota_url));
    portEXIT_CRITICAL(&s_cfg_mux);
    ESP_LOGI(TAG, "ota config cleared");
}
