#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "settings";
#define NS  "cdjcfg"

/* Defaults match the firmware's out-of-the-box behaviour. */
static app_settings_t s_cfg = {
    .audio_out     = 0,   /* built-in monitor speaker */
    .backlight_pct = 80,
    .time_remain   = 0,
    .cue_mode      = 0,   /* stereo master */
    .master_trim_preset = 0, /* 0 dB */
    .wifi_remote   = 0,   /* Wi-Fi remote off by default */
};


/* Defined with the rest of the pull-OTA config at the bottom of the file. */
static void load_ota_config(nvs_handle_t h);

static void save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(rw) failed for %s", key);
        return;
    }
    nvs_set_u8(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t app_settings_init(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s) — erasing", esp_err_to_name(rc));
        nvs_flash_erase();
        rc = nvs_flash_init();
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(rc));
        return rc;
    }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "audio_out", &v) == ESP_OK) s_cfg.audio_out     = v;
        if (nvs_get_u8(h, "backlight", &v) == ESP_OK) s_cfg.backlight_pct = v > 100 ? 100 : v;
        if (nvs_get_u8(h, "time_rem",  &v) == ESP_OK) s_cfg.time_remain   = v;
        if (nvs_get_u8(h, "cue_mode",  &v) == ESP_OK && v <= 1) s_cfg.cue_mode = v;
        if (nvs_get_u8(h, "master_trim", &v) == ESP_OK && v <= 2) s_cfg.master_trim_preset = v;
        if (nvs_get_u8(h, "wifi_rem",   &v) == ESP_OK && v <= 1) s_cfg.wifi_remote = v;
        load_ota_config(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "loaded: monitor_speaker=%s backlight=%u time_remain=%u cue_mode=%u master_trim=%u wifi_remote=%s",
             s_cfg.audio_out ? "off" : "on",
             s_cfg.backlight_pct, s_cfg.time_remain, s_cfg.cue_mode,
             s_cfg.master_trim_preset,
             s_cfg.wifi_remote ? "on" : "off");
    return ESP_OK;
}

app_settings_t app_settings_get(void)
{
    return s_cfg;
}

void app_settings_set_audio_out(uint8_t out)
{
    if (s_cfg.audio_out == out) return;
    s_cfg.audio_out = out;
    save_u8("audio_out", out);
}

void app_settings_set_backlight(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (s_cfg.backlight_pct == pct) return;
    s_cfg.backlight_pct = pct;
    save_u8("backlight", pct);
}

void app_settings_set_time_remain(uint8_t remain)
{
    if (s_cfg.time_remain == remain) return;
    s_cfg.time_remain = remain;
    save_u8("time_rem", remain);
}

void app_settings_set_cue_mode(uint8_t mode)
{
    if (mode > 1) mode = 0;
    if (s_cfg.cue_mode == mode) return;
    s_cfg.cue_mode = mode;
    save_u8("cue_mode", mode);
}

void app_settings_set_master_trim_preset(uint8_t preset)
{
    if (preset > 2) preset = 0;
    if (s_cfg.master_trim_preset == preset) return;
    s_cfg.master_trim_preset = preset;
    save_u8("master_trim", preset);
}

void app_settings_set_wifi_remote(uint8_t on)
{
    on = on ? 1 : 0;
    if (s_cfg.wifi_remote == on) return;
    s_cfg.wifi_remote = on;
    save_u8("wifi_rem", on);
}

/* ── Pull-OTA service network ─────────────────────────────────────────────
 *
 * Held outside app_settings_t on purpose; see the header. The passphrase is
 * never logged here, not even its length, and there is no path that returns
 * it to a caller other than app_settings_ota_copy_password().
 */
static char s_ota_ssid[APP_SETTINGS_OTA_SSID_CAP];
static char s_ota_pass[APP_SETTINGS_OTA_PASS_CAP];
static char s_ota_url[APP_SETTINGS_OTA_URL_CAP];

static void copy_bounded(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0u) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strnlen(src, cap - 1u);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void load_ota_config(nvs_handle_t h)
{
    size_t len = sizeof(s_ota_ssid);
    if (nvs_get_str(h, "ota_ssid", s_ota_ssid, &len) != ESP_OK) s_ota_ssid[0] = '\0';
    len = sizeof(s_ota_pass);
    if (nvs_get_str(h, "ota_pass", s_ota_pass, &len) != ESP_OK) s_ota_pass[0] = '\0';
    len = sizeof(s_ota_url);
    if (nvs_get_str(h, "ota_url", s_ota_url, &len) != ESP_OK) s_ota_url[0] = '\0';
}

void app_settings_ota_get_ssid(char *out, size_t cap) { copy_bounded(out, cap, s_ota_ssid); }
void app_settings_ota_get_url(char *out, size_t cap)  { copy_bounded(out, cap, s_ota_url); }
bool app_settings_ota_has_password(void)              { return s_ota_pass[0] != '\0'; }
void app_settings_ota_copy_password(char *out, size_t cap) { copy_bounded(out, cap, s_ota_pass); }

esp_err_t app_settings_ota_set(const char *ssid, const char *password, const char *url)
{
    if (ssid) copy_bounded(s_ota_ssid, sizeof(s_ota_ssid), ssid);
    if (url)  copy_bounded(s_ota_url,  sizeof(s_ota_url),  url);
    /* NULL keeps the stored passphrase so the SSID or URL can be corrected
     * without retyping it; "" clears it for an open network. */
    if (password) copy_bounded(s_ota_pass, sizeof(s_ota_pass), password);

    nvs_handle_t h;
    esp_err_t rc = nvs_open(NS, NVS_READWRITE, &h);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(rw) failed for ota config");
        return rc;
    }
    nvs_set_str(h, "ota_ssid", s_ota_ssid);
    nvs_set_str(h, "ota_pass", s_ota_pass);
    nvs_set_str(h, "ota_url", s_ota_url);
    rc = nvs_commit(h);
    nvs_close(h);
    /* SSID and URL only. */
    ESP_LOGI(TAG, "ota config saved: ssid=\"%s\" url=\"%s\" password=%s",
             s_ota_ssid, s_ota_url,
             s_ota_pass[0] ? "set" : "none");
    return rc;
}

void app_settings_ota_clear(void)
{
    memset(s_ota_ssid, 0, sizeof(s_ota_ssid));
    memset(s_ota_pass, 0, sizeof(s_ota_pass));
    memset(s_ota_url, 0, sizeof(s_ota_url));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "ota_ssid");
    nvs_erase_key(h, "ota_pass");
    nvs_erase_key(h, "ota_url");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "ota config cleared");
}
