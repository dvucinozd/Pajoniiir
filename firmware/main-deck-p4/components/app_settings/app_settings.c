#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";
#define NS  "cdjcfg"

/* Defaults match the firmware's out-of-the-box behaviour. */
static app_settings_t s_cfg = {
    .audio_out     = 0,   /* built-in monitor speaker */
    .backlight_pct = 80,
    .time_remain   = 0,
    .cue_mode      = 0,   /* stereo master */
};


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
        if (nvs_get_u8(h, "backlight", &v) == ESP_OK) s_cfg.backlight_pct = v;
        if (nvs_get_u8(h, "time_rem",  &v) == ESP_OK) s_cfg.time_remain   = v;
        if (nvs_get_u8(h, "cue_mode",  &v) == ESP_OK && v <= 1) s_cfg.cue_mode = v;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "loaded: monitor_speaker=%s backlight=%u time_remain=%u cue_mode=%u",
             s_cfg.audio_out ? "off" : "on",
             s_cfg.backlight_pct, s_cfg.time_remain, s_cfg.cue_mode);
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
