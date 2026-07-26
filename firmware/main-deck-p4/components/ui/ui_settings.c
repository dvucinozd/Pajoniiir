#include "ui_settings.h"
#include "control_link.h"
#ifndef WIN32
#if CONFIG_AUDIO_RECORDER_ENABLED
#include "audio_recorder.h"
#endif
#include "service_log.h"
#endif

#include <limits.h>
#include <stdio.h>
#include <string.h>

bool ui_settings_should_poll(uint32_t now_ms,
                             uint32_t last_poll_ms,
                             bool force,
                             uint32_t interval_ms)
{
    return force || last_poll_ms == 0 || (uint32_t)(now_ms - last_poll_ms) >= interval_ms;
}

const char *ui_settings_cue_mode_name(uint8_t mode)
{
    switch (mode) {
    case 1:
        return "CUE: SPLIT MONO";
    default:
        return "CUE: STEREO";
    }
}

typedef struct {
    const char *label;
    float gain;
} ui_settings_master_trim_preset_t;

static const ui_settings_master_trim_preset_t s_master_trim_presets[] = {
    { "MASTER: 0 dB", 1.0f },
    { "MASTER: -3 dB", 0.7079458f },
    { "MASTER: -6 dB", 0.5011872f },
};

uint8_t ui_settings_master_trim_preset_count(void)
{
    return (uint8_t)(sizeof(s_master_trim_presets) / sizeof(s_master_trim_presets[0]));
}

uint8_t ui_settings_master_trim_sanitize_preset(uint8_t preset)
{
    return preset < ui_settings_master_trim_preset_count() ? preset : 0u;
}

uint8_t ui_settings_master_trim_next_preset(uint8_t current)
{
    uint8_t count = ui_settings_master_trim_preset_count();
    if (current >= count || count == 0u) {
        return 0u;
    }
    return (uint8_t)((current + 1u) % count);
}

float ui_settings_master_trim_gain(uint8_t preset)
{
    preset = ui_settings_master_trim_sanitize_preset(preset);
    return s_master_trim_presets[preset].gain;
}

const char *ui_settings_master_trim_label(uint8_t preset)
{
    preset = ui_settings_master_trim_sanitize_preset(preset);
    return s_master_trim_presets[preset].label;
}

const char *ui_settings_s3_debug_ap_status_label(uint8_t status)
{
    switch (status) {
    case CTRL_S3_DEBUG_AP_OFF:
        return "OFF";
    case CTRL_S3_DEBUG_AP_STARTING:
        return "STARTING";
    case CTRL_S3_DEBUG_AP_ON:
        return "ON";
    case CTRL_S3_DEBUG_AP_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *ui_settings_firmware_slot_label(uint8_t slot)
{
    switch (slot) {
    case CTRL_FW_SLOT_OTA_0: return "ota_0";
    case CTRL_FW_SLOT_OTA_1: return "ota_1";
    case CTRL_FW_SLOT_FACTORY: return "factory";
    default: return "unknown";
    }
}

const char *ui_settings_firmware_state_label(uint8_t state)
{
    switch (state) {
    case CTRL_FW_STATE_NEW: return "NEW";
    case CTRL_FW_STATE_PENDING_VERIFY: return "PENDING";
    case CTRL_FW_STATE_VALID: return "VALID";
    case CTRL_FW_STATE_INVALID: return "INVALID";
    case CTRL_FW_STATE_ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    }
}

bool ui_settings_is_active_tab(int active_tab, int settings_tab_index)
{
    return settings_tab_index >= 0 && active_tab == settings_tab_index;
}

#ifndef UI_SETTINGS_HOST_TEST

#include "esp_log.h"
#include "ui_theme.h"

#ifndef WIN32
#include "app_settings.h"
#include "audio_engine.h"
#include "bsp_jc4880.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "firmware_health.h"
#endif

static const char *TAG = "ui_settings";

typedef struct {
    bool valid;
    char text[80];
} ui_settings_text_cache_t;

typedef struct {
    bool valid;
    uint32_t color;
} ui_settings_color_cache_t;

static ui_settings_config_t s_config;
static ui_settings_widgets_t s_widgets;
static lv_obj_t *s_label_brightness_val = NULL;
static lv_obj_t *s_label_cue_mode = NULL;
static lv_obj_t *s_label_master_trim = NULL;
static lv_obj_t *s_label_wifi_remote = NULL;
static lv_obj_t *s_label_s3_debug_ap = NULL;
static lv_obj_t *s_switch_s3_debug_ap = NULL;
static lv_obj_t *s_label_s3_firmware = NULL;
static uint8_t s_master_trim_preset = 0;
static ui_settings_wifi_toggle_cb_t s_wifi_toggle_cb = NULL;
static ui_settings_recording_toggle_cb_t s_recording_toggle_cb = NULL;
#if CONFIG_AUDIO_RECORDER_ENABLED
static lv_obj_t *s_btn_rec = NULL;
static lv_obj_t *s_label_rec_btn = NULL;
static lv_obj_t *s_label_rec_status = NULL;
#endif
static lv_obj_t *s_label_svc_log = NULL;
static ui_settings_s3_debug_ap_toggle_cb_t s_s3_debug_ap_toggle_cb = NULL;
static volatile uint8_t s_s3_debug_ap_status = CTRL_S3_DEBUG_AP_OFF;
static uint8_t s_s3_debug_ap_displayed_status = UINT8_MAX;
static ui_settings_color_cache_t s_cache_uart_color;
static ui_settings_color_cache_t s_cache_sd_color;
static int s_cache_uart_state = -1;
static uint32_t s_cache_uart_age_bucket = UINT32_MAX;
static int s_cache_sd_state = -1;
static uint32_t s_cache_sd_free_mib = UINT32_MAX;
static uint32_t s_cache_sd_total_mib = UINT32_MAX;
static uint32_t s_cache_sd_last_poll_ms = 0;
static ui_settings_text_cache_t s_cache_sd_text;
static ui_settings_text_cache_t s_cache_s3_firmware_text;
static ui_settings_color_cache_t s_cache_s3_firmware_color;

static void ui_settings_copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t i = 0;
    while (i + 1u < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ui_settings_label_set_text_cached(lv_obj_t *label,
                                              ui_settings_text_cache_t *cache,
                                              const char *text)
{
    if (!label || !cache) {
        return;
    }
    const char *safe_text = text ? text : "";
    if (cache->valid && strncmp(cache->text, safe_text, sizeof(cache->text)) == 0) {
        return;
    }
    lv_label_set_text(label, safe_text);
    ui_settings_copy_str(cache->text, sizeof(cache->text), safe_text);
    cache->valid = true;
}

static void ui_settings_obj_set_text_color_cached(lv_obj_t *obj,
                                                  ui_settings_color_cache_t *cache,
                                                  lv_color_t color)
{
    if (!obj || !cache) {
        return;
    }
    uint32_t color_u32 = lv_color_to_u32(color);
    if (cache->valid && cache->color == color_u32) {
        return;
    }
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    cache->color = color_u32;
    cache->valid = true;
}

static void ui_settings_label_small_caps(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

static lv_obj_t *ui_settings_section(lv_obj_t *parent,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     const char *title)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    if (s_config.panel_frame) {
        lv_obj_add_style(section, s_config.panel_frame, LV_PART_MAIN);
    }
    lv_obj_set_size(section, w, h);
    lv_obj_set_pos(section, x, y);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(section);
    ui_settings_label_small_caps(label, title, COL_TEXT_MUTED);
    lv_obj_set_pos(label, 14, 12);
    return section;
}

static lv_obj_t *ui_settings_value_label(lv_obj_t *parent,
                                         const char *text,
                                         lv_color_t color,
                                         const lv_font_t *font,
                                         int x,
                                         int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *ui_settings_static_tile(lv_obj_t *parent,
                                         int x,
                                         int y,
                                         int w,
                                         int h,
                                         const char *text,
                                         lv_color_t text_color,
                                         lv_color_t fill_color,
                                         lv_color_t border_color)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_style_bg_color(tile, fill_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, border_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 2, LV_PART_MAIN);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_pos(tile, x, y);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return tile;
}

static void ui_settings_style_wireless_switch(lv_obj_t *sw, lv_color_t active_color)
{
    if (!sw) {
        return;
    }

    lv_obj_set_size(sw, 54, 24);
    lv_obj_set_style_bg_color(sw, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, 12, LV_PART_MAIN);

    lv_obj_set_style_bg_color(sw, COL_PANEL_DK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, active_color, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(sw, COL_DISABLED, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, COL_BG, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_CHECKED);
}

static void slider_brightness_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int val = lv_slider_get_value(slider);
    lv_label_set_text_fmt(s_label_brightness_val, "%d%%", val);
#ifndef WIN32
    bsp_display_set_backlight((uint8_t)val);
    app_settings_set_backlight((uint8_t)val);
#endif
    ESP_LOGI(TAG, "Backlight brightness set to %d%%", val);
}

#ifndef WIN32
static const char *ui_settings_reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External pin";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "PANIC / exception";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (power dip)";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB peripheral";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "Unknown";
    }
}
#endif

static void wifi_remote_event_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
#ifndef WIN32
    app_settings_set_wifi_remote(on ? 1 : 0);
#endif
    if (s_wifi_toggle_cb) {
        s_wifi_toggle_cb(on);
    }
    if (s_label_wifi_remote) {
        lv_label_set_text(s_label_wifi_remote, on ? "P4 REMOTE: ON" : "P4 REMOTE: OFF");
        lv_obj_set_style_text_color(s_label_wifi_remote,
                                    on ? COL_GREEN : COL_TEXT_DIM, LV_PART_MAIN);
    }
    ESP_LOGI(TAG, "Wi-Fi remote: %s", on ? "on" : "off");
}

#if !defined(WIN32) && CONFIG_AUDIO_RECORDER_ENABLED
static void ui_settings_update_recording_label(void)
{
    audio_recorder_status_t st;
    if (audio_recorder_get_status(&st) != ESP_OK) {
        return;
    }
    bool active = (st.state == AUDIO_RECORDER_RECORDING ||
                   st.state == AUDIO_RECORDER_STARTING ||
                   st.state == AUDIO_RECORDER_STOPPING);
    if (s_label_rec_btn) {
        lv_label_set_text(s_label_rec_btn, active ? "STOP REC" : "RECORD");
    }
    if (s_label_rec_status) {
        char buf[48];
        lv_color_t col = COL_TEXT_DIM;
        if (st.state == AUDIO_RECORDER_ERROR) {
            snprintf(buf, sizeof(buf), "SD error - press to reset");
            col = COL_RED;
        } else if (active && st.sample_rate > 0u) {
            uint32_t secs = (uint32_t)(st.frames_written / st.sample_rate);
            snprintf(buf, sizeof(buf), "REC %02u:%02u  %llu MB",
                     (unsigned)(secs / 60u), (unsigned)(secs % 60u),
                     (unsigned long long)(st.bytes_written >> 20));
            col = COL_RED;
        } else {
            snprintf(buf, sizeof(buf), "Master out -> /sd/recordings");
        }
        lv_label_set_text(s_label_rec_status, buf);
        lv_obj_set_style_text_color(s_label_rec_status, col, LV_PART_MAIN);
    }
}
#endif  /* !WIN32 && CONFIG_AUDIO_RECORDER_ENABLED */

#ifndef WIN32
static void ui_settings_update_service_log_label(void)
{
    if (!s_label_svc_log) {
        return;
    }
    service_log_status_t st;
    if (service_log_get_status(&st) != ESP_OK) {
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "SD Log: %s  %luKB  drop %lu",
             st.available ? "OK" : "off",
             (unsigned long)(st.current_bytes >> 10),
             (unsigned long)st.dropped);
    lv_label_set_text(s_label_svc_log, buf);
    lv_obj_set_style_text_color(s_label_svc_log,
                                st.dropped > 0u ? COL_RED
                                    : (st.available ? COL_TEXT_DIM : COL_TEXT_MUTED),
                                LV_PART_MAIN);
}
#endif  /* !WIN32 */

#if !defined(WIN32) && CONFIG_AUDIO_RECORDER_ENABLED
static void recording_event_cb(lv_event_t *event)
{
    (void)event;
    if (!s_recording_toggle_cb) {
        return;
    }
    audio_recorder_state_t st = audio_recorder_get_state();
    bool active = (st == AUDIO_RECORDER_RECORDING || st == AUDIO_RECORDER_STARTING);
    bool ok = s_recording_toggle_cb(!active);
    ESP_LOGI(TAG, "recording toggle -> %s (%s)", active ? "stop" : "start",
             ok ? "ok" : "failed");
    ui_settings_update_recording_label();
}
#endif  /* !WIN32 && CONFIG_AUDIO_RECORDER_ENABLED */

void ui_settings_set_recording_toggle_cb(ui_settings_recording_toggle_cb_t cb)
{
    s_recording_toggle_cb = cb;
}

static lv_color_t s3_debug_ap_status_color(uint8_t status)
{
    switch (status) {
    case CTRL_S3_DEBUG_AP_ON:
        return COL_GREEN;
    case CTRL_S3_DEBUG_AP_STARTING:
        return COL_AMBER;
    case CTRL_S3_DEBUG_AP_ERROR:
        return COL_RED;
    default:
        return COL_TEXT_DIM;
    }
}

static void ui_settings_apply_s3_debug_ap_status(void)
{
    uint8_t status = s_s3_debug_ap_status;
    if (!s_label_s3_debug_ap || s_s3_debug_ap_displayed_status == status) {
        return;
    }

    lv_label_set_text_fmt(s_label_s3_debug_ap, "S3 DEBUG AP: %s",
                          ui_settings_s3_debug_ap_status_label(status));
    lv_obj_set_style_text_color(s_label_s3_debug_ap,
                                s3_debug_ap_status_color(status),
                                LV_PART_MAIN);
    if (s_switch_s3_debug_ap) {
        if (status == CTRL_S3_DEBUG_AP_ON || status == CTRL_S3_DEBUG_AP_STARTING) {
            lv_obj_add_state(s_switch_s3_debug_ap, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_switch_s3_debug_ap, LV_STATE_CHECKED);
        }
    }
    s_s3_debug_ap_displayed_status = status;
}

static void s3_debug_ap_event_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    s_s3_debug_ap_status = on ? CTRL_S3_DEBUG_AP_STARTING : CTRL_S3_DEBUG_AP_OFF;
    s_s3_debug_ap_displayed_status = UINT8_MAX;
    ui_settings_apply_s3_debug_ap_status();

    if (s_s3_debug_ap_toggle_cb) {
        s_s3_debug_ap_toggle_cb(on);
    }
    ESP_LOGI(TAG, "S3 Debug AP requested: %s", on ? "on" : "off");
}

#ifndef WIN32
static void master_trim_event_cb(lv_event_t *event)
{
    (void)event;
    s_master_trim_preset = ui_settings_master_trim_next_preset(s_master_trim_preset);
    float gain = ui_settings_master_trim_gain(s_master_trim_preset);
    audio_engine_set_master_trim(gain);
    app_settings_set_master_trim_preset(s_master_trim_preset);
    if (s_label_master_trim) {
        lv_label_set_text(s_label_master_trim, ui_settings_master_trim_label(s_master_trim_preset));
    }
    ESP_LOGI(TAG, "Master trim set: %s (gain %.3f)",
             ui_settings_master_trim_label(s_master_trim_preset),
             (double)gain);
}

static void cue_mode_event_cb(lv_event_t *event)
{
    (void)event;
    app_settings_t cfg = app_settings_get();
    uint8_t next = (uint8_t)((cfg.cue_mode + 1u) % 2u);
    app_settings_set_cue_mode(next);
    audio_engine_set_cue_mode(next);
    if (s_label_cue_mode) {
        lv_label_set_text_fmt(s_label_cue_mode, "%s", ui_settings_cue_mode_name(next));
    }
    ESP_LOGI(TAG, "Cue mode saved: %s", ui_settings_cue_mode_name(next));
}

#endif

void ui_settings_set_wifi_toggle_cb(ui_settings_wifi_toggle_cb_t cb)
{
    s_wifi_toggle_cb = cb;
}

void ui_settings_set_s3_debug_ap_toggle_cb(ui_settings_s3_debug_ap_toggle_cb_t cb)
{
    s_s3_debug_ap_toggle_cb = cb;
}

void ui_settings_set_s3_debug_ap_status(uint8_t status)
{
    s_s3_debug_ap_status = status;
    s_s3_debug_ap_displayed_status = UINT8_MAX;
}

void ui_settings_configure(const ui_settings_config_t *config)
{
    s_config = (ui_settings_config_t){0};
    if (config) {
        s_config = *config;
    }
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    if (s_config.screen_bg) {
        lv_obj_add_style(screen, s_config.screen_bg, LV_PART_MAIN);
    }
    lv_obj_set_size(screen, s_config.hor_res, s_config.content_h);
    lv_obj_set_pos(screen, 0, s_config.content_y);

#ifndef WIN32
    app_settings_t cfg = app_settings_get();
    int bl_init = cfg.backlight_pct;
    s_master_trim_preset = ui_settings_master_trim_sanitize_preset(cfg.master_trim_preset);
    audio_engine_set_master_trim(ui_settings_master_trim_gain(s_master_trim_preset));
    bool wifi_remote_init = (cfg.wifi_remote != 0);
#else
    int bl_init = 80;
    s_master_trim_preset = 0;
    bool wifi_remote_init = false;
#endif

    const int left_x = 30;
    const int left_w = 350;

    lv_obj_t *display_section = ui_settings_section(screen, left_x, 20, left_w, 86, "DISPLAY");
    lv_obj_t *slider_backlight = lv_slider_create(display_section);
    lv_obj_set_size(slider_backlight, 230, 18);
    lv_obj_set_pos(slider_backlight, 16, 48);
    lv_slider_set_range(slider_backlight, 10, 100);
    lv_slider_set_value(slider_backlight, bl_init, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_backlight, slider_brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_brightness_val = ui_settings_value_label(display_section, "", COL_TEXT,
                                                     &lv_font_montserrat_14, 270, 44);
    lv_label_set_text_fmt(s_label_brightness_val, "%d%%", bl_init);

    lv_obj_t *master_section = ui_settings_section(screen, left_x, 118, left_w, 86, "MASTER OUTPUT");
    lv_obj_t *btn_master_trim = lv_button_create(master_section);
    lv_obj_remove_style_all(btn_master_trim);
    if (s_config.btn_secondary) {
        lv_obj_add_style(btn_master_trim, s_config.btn_secondary, LV_PART_MAIN);
    }
    if (s_config.pressed) {
        lv_obj_add_style(btn_master_trim, s_config.pressed, LV_STATE_PRESSED);
    }
    lv_obj_set_size(btn_master_trim, 168, 32);
    lv_obj_set_pos(btn_master_trim, 16, 40);
#ifndef WIN32
    lv_obj_add_event_cb(btn_master_trim, master_trim_event_cb, LV_EVENT_CLICKED, NULL);
#endif

    s_label_master_trim = lv_label_create(btn_master_trim);
    lv_label_set_text(s_label_master_trim, ui_settings_master_trim_label(s_master_trim_preset));
    lv_obj_set_style_text_font(s_label_master_trim, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_master_trim, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_label_master_trim, LV_ALIGN_CENTER, 0, 0);

    ui_settings_value_label(master_section,
                            "Lower if limiter stays active",
                            COL_TEXT_DIM,
                            &lv_font_montserrat_12,
                            198,
                            48);

    lv_obj_t *output_section = ui_settings_section(screen, left_x, 216, left_w, 86, "OUTPUT");
    ui_settings_value_label(output_section,
                            "MAIN: PCM5102A RCA",
                            COL_GREEN,
                            &lv_font_montserrat_12,
                            16,
                            36);
    ui_settings_value_label(output_section,
                            "CUE: FLX4 USB",
                            COL_ACCENT,
                            &lv_font_montserrat_12,
                            16,
                            56);
    ui_settings_value_label(output_section,
#if defined(CONFIG_BSP_ES8311_MONITOR) && CONFIG_BSP_ES8311_MONITOR
                            "LOCAL: ES8311 monitor",
                            COL_TEXT_DIM,
#else
                            "LOCAL: disabled",
                            COL_TEXT_DIM,
#endif
                            &lv_font_montserrat_12,
                            176,
                            56);

#if CONFIG_AUDIO_RECORDER_ENABLED
    /* Compact section that fits the gap between OUTPUT and the full-width
     * MIXER STATUS bar (y=356): button and status share one row. */
    lv_obj_t *rec_section = ui_settings_section(screen, left_x, 304, left_w, 48, "RECORDING");
    s_btn_rec = lv_button_create(rec_section);
    lv_obj_remove_style_all(s_btn_rec);
    if (s_config.btn_secondary) {
        lv_obj_add_style(s_btn_rec, s_config.btn_secondary, LV_PART_MAIN);
    }
    if (s_config.pressed) {
        lv_obj_add_style(s_btn_rec, s_config.pressed, LV_STATE_PRESSED);
    }
    lv_obj_set_size(s_btn_rec, 150, 24);
    lv_obj_set_pos(s_btn_rec, 16, 20);
#ifndef WIN32
    lv_obj_add_event_cb(s_btn_rec, recording_event_cb, LV_EVENT_CLICKED, NULL);
#endif
    s_label_rec_btn = lv_label_create(s_btn_rec);
    lv_label_set_text(s_label_rec_btn, "RECORD");
    lv_obj_set_style_text_font(s_label_rec_btn, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_rec_btn, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_label_rec_btn, LV_ALIGN_CENTER, 0, 0);

    s_label_rec_status = ui_settings_value_label(rec_section,
                                                 "-> /sd/recordings",
                                                 COL_TEXT_DIM,
                                                 &lv_font_montserrat_12, 176, 24);
    lv_obj_set_width(s_label_rec_status, 160);
    lv_label_set_long_mode(s_label_rec_status, LV_LABEL_LONG_CLIP);

#endif  /* CONFIG_AUDIO_RECORDER_ENABLED */

    lv_obj_t *status_section = ui_settings_section(screen, 410, 20, 360, 210, "SYSTEM STATUS");

    lv_obj_t *label_uart_status =
        ui_settings_value_label(status_section,
                                "Control Link (S3): Offline (no heartbeat)",
                                COL_RED, &lv_font_montserrat_12, 16, 40);
    lv_obj_set_width(label_uart_status, 320);
    lv_label_set_long_mode(label_uart_status, LV_LABEL_LONG_CLIP);

    ui_settings_value_label(status_section, "SD Card", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 76);
    lv_obj_t *label_sd_status =
        ui_settings_value_label(status_section, "Checking /sd...",
                                COL_TEXT_DIM, &lv_font_montserrat_12, 16, 96);
    lv_obj_set_width(label_sd_status, 320);
    lv_label_set_long_mode(label_sd_status, LV_LABEL_LONG_CLIP);

    s_label_svc_log = ui_settings_value_label(status_section, "SD Log: --",
                                              COL_TEXT_DIM, &lv_font_montserrat_12, 16, 118);
    lv_obj_set_width(s_label_svc_log, 320);
    lv_label_set_long_mode(s_label_svc_log, LV_LABEL_LONG_CLIP);

#ifndef WIN32
    {
        firmware_health_info_t info;
        char p4_text[80];
        if (firmware_health_get_info(&info) == ESP_OK) {
            snprintf(p4_text, sizeof(p4_text), "P4: %s [%s]",
                     info.version, info.partition_label);
        } else {
            snprintf(p4_text, sizeof(p4_text), "P4: firmware status unavailable");
        }
        ui_settings_value_label(status_section, p4_text,
                                COL_GREEN, &lv_font_montserrat_12, 16, 146);
    }
#else
    {
        ui_settings_value_label(status_section, "P4: Simulator Mode",
                                COL_GREEN, &lv_font_montserrat_12, 16, 146);
    }
#endif
    s_label_s3_firmware =
        ui_settings_value_label(status_section, "S3: waiting for firmware report",
                                COL_TEXT_DIM, &lv_font_montserrat_12, 16, 168);
    lv_obj_set_width(s_label_s3_firmware, 320);
    lv_label_set_long_mode(s_label_s3_firmware, LV_LABEL_LONG_CLIP);

#ifndef WIN32
    {
        esp_reset_reason_t rr = esp_reset_reason();
        bool rr_bad = (rr == ESP_RST_PANIC || rr == ESP_RST_BROWNOUT ||
                       rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
                       rr == ESP_RST_WDT);
        char rr_buf[64];
        snprintf(rr_buf, sizeof(rr_buf), "Last reset: %s", ui_settings_reset_reason_str());
        ui_settings_value_label(status_section, rr_buf,
                                rr_bad ? COL_RED : COL_TEXT_DIM,
                                &lv_font_montserrat_12, 16, 190);
    }
#endif

    lv_obj_t *wifi_section = ui_settings_section(screen, 410, 240, 360, 108, "WIRELESS");
    lv_obj_t *sw_wifi = lv_switch_create(wifi_section);
    ui_settings_style_wireless_switch(sw_wifi, COL_GREEN);
    lv_obj_set_pos(sw_wifi, 16, 38);
    lv_obj_add_event_cb(sw_wifi, wifi_remote_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (wifi_remote_init) {
        lv_obj_add_state(sw_wifi, LV_STATE_CHECKED);
    }

    s_label_wifi_remote = ui_settings_value_label(wifi_section,
                                                  wifi_remote_init ? "P4 REMOTE: ON" : "P4 REMOTE: OFF",
                                                  wifi_remote_init ? COL_GREEN : COL_TEXT_DIM,
                                                  &lv_font_montserrat_14,
                                                  96, 41);

    s_switch_s3_debug_ap = lv_switch_create(wifi_section);
    ui_settings_style_wireless_switch(s_switch_s3_debug_ap, COL_ACCENT);
    lv_obj_set_pos(s_switch_s3_debug_ap, 16, 74);
    lv_obj_add_event_cb(s_switch_s3_debug_ap, s3_debug_ap_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_s3_debug_ap = ui_settings_value_label(wifi_section,
                                                  "S3 DEBUG AP: OFF",
                                                  COL_TEXT_DIM,
                                                  &lv_font_montserrat_14,
                                                  96, 77);
    s_s3_debug_ap_displayed_status = UINT8_MAX;
    ui_settings_apply_s3_debug_ap_status();

    lv_obj_t *mixer_section = ui_settings_section(screen, 30, 356, 740, 64, "MIXER STATUS");
    ui_settings_static_tile(mixer_section, 18, 34, 110, 22,
                            "MIXER: FLX4", COL_TEXT_MUTED, COL_PANEL_DK, COL_BORDER);
    ui_settings_static_tile(mixer_section, 140, 34, 104, 22,
                            "CH FADERS", COL_ACCENT, COL_PANEL_DK, COL_BORDER);
    ui_settings_static_tile(mixer_section, 256, 34, 112, 22,
                            "CROSSFADER", COL_TEXT_MUTED, COL_PANEL_DK, COL_BORDER);
    ui_settings_static_tile(mixer_section, 380, 34, 104, 22,
                            "PFL D1/D2", COL_AMBER, COL_PANEL_DK, COL_BORDER);

    lv_obj_t *btn_cue = lv_button_create(mixer_section);
    lv_obj_remove_style_all(btn_cue);
    if (s_config.btn_secondary) {
        lv_obj_add_style(btn_cue, s_config.btn_secondary, LV_PART_MAIN);
    }
    if (s_config.pressed) {
        lv_obj_add_style(btn_cue, s_config.pressed, LV_STATE_PRESSED);
    }
    lv_obj_set_size(btn_cue, 142, 22);
    lv_obj_set_pos(btn_cue, 570, 34);
#ifndef WIN32
    lv_obj_add_event_cb(btn_cue, cue_mode_event_cb, LV_EVENT_CLICKED, NULL);
#endif

    s_label_cue_mode = lv_label_create(btn_cue);
#ifndef WIN32
    lv_label_set_text_fmt(s_label_cue_mode, "%s", ui_settings_cue_mode_name(cfg.cue_mode));
#else
    lv_label_set_text(s_label_cue_mode, "CUE: STEREO");
#endif
    lv_obj_set_style_text_font(s_label_cue_mode, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_cue_mode, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_label_cue_mode, LV_ALIGN_CENTER, 0, 0);

    ui_settings_widgets_t settings_widgets = {
        .uart_status = label_uart_status,
        .sd_status = label_sd_status,
    };
    ui_settings_init(&settings_widgets);
    ui_settings_refresh_storage();

    return screen;
}

void ui_settings_init(const ui_settings_widgets_t *widgets)
{
    memset(&s_widgets, 0, sizeof(s_widgets));
    if (widgets) {
        s_widgets = *widgets;
    }
    ui_settings_invalidate();
}

void ui_settings_invalidate(void)
{
    s_cache_uart_color.valid = false;
    s_cache_sd_color.valid = false;
    s_cache_uart_state = -1;
    s_cache_uart_age_bucket = UINT32_MAX;
    s_cache_sd_state = -1;
    s_cache_sd_free_mib = UINT32_MAX;
    s_cache_sd_total_mib = UINT32_MAX;
    s_cache_sd_last_poll_ms = 0;
    s_cache_sd_text.valid = false;
    s_cache_s3_firmware_text.valid = false;
    s_cache_s3_firmware_color.valid = false;
}

static void ui_settings_format_storage_size(uint64_t bytes, char *out, size_t out_size)
{
    const uint64_t gib = 1024ull * 1024ull * 1024ull;
    const uint64_t mib = 1024ull * 1024ull;
    uint64_t scale = mib;
    const char *unit = "MB";
    if (bytes >= gib) {
        scale = gib;
        unit = "GB";
    }

    uint64_t whole = bytes / scale;
    uint64_t frac = ((bytes % scale) * 10ull) / scale;
    snprintf(out, out_size, "%llu.%llu %s",
             (unsigned long long)whole,
             (unsigned long long)frac,
             unit);
}

#ifndef WIN32
static void ui_settings_update_uart_status_label(const deck_state_t *state)
{
    if (!s_widgets.uart_status || !state) {
        return;
    }

    int display_state;
    uint32_t age_bucket;
    if (state->control_link_connected) {
        display_state = 1;
        if (state->last_heartbeat_age_ms < 1000u) {
            age_bucket = state->last_heartbeat_age_ms / 100u;
            if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
                lv_label_set_text_fmt(s_widgets.uart_status,
                                      "Control Link (S3): Connected (age %lu ms)",
                                      (unsigned long)(age_bucket * 100u));
                s_cache_uart_state = display_state;
                s_cache_uart_age_bucket = age_bucket;
            }
        } else {
            age_bucket = state->last_heartbeat_age_ms / 1000u;
            if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
                lv_label_set_text_fmt(s_widgets.uart_status,
                                      "Control Link (S3): Connected (age %lu s)",
                                      (unsigned long)age_bucket);
                s_cache_uart_state = display_state;
                s_cache_uart_age_bucket = age_bucket;
            }
        }
        ui_settings_obj_set_text_color_cached(s_widgets.uart_status, &s_cache_uart_color, COL_GREEN);
        return;
    }

    if (state->last_heartbeat_age_ms == UINT32_MAX) {
        display_state = 0;
        age_bucket = UINT32_MAX;
        if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
            lv_label_set_text(s_widgets.uart_status, "Control Link (S3): Offline (no heartbeat)");
            s_cache_uart_state = display_state;
            s_cache_uart_age_bucket = age_bucket;
        }
    } else {
        display_state = 2;
        age_bucket = state->last_heartbeat_age_ms / 1000u;
        if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
            lv_label_set_text_fmt(s_widgets.uart_status,
                                  "Control Link (S3): Offline (last %lu s ago)",
                                  (unsigned long)age_bucket);
            s_cache_uart_state = display_state;
            s_cache_uart_age_bucket = age_bucket;
        }
    }
    ui_settings_obj_set_text_color_cached(s_widgets.uart_status, &s_cache_uart_color, COL_RED);
}

static void ui_settings_update_sd_status_label(bool force)
{
    if (!s_widgets.sd_status) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ull);
    if (!ui_settings_should_poll(now_ms, s_cache_sd_last_poll_ms, force, 1000u)) {
        return;
    }
    s_cache_sd_last_poll_ms = now_ms;

    bsp_sd_status_t status;
    esp_err_t rc = bsp_sd_get_status(&status);
    if (rc != ESP_OK || !status.mounted) {
        if (s_cache_sd_state != 0) {
            ui_settings_label_set_text_cached(s_widgets.sd_status,
                                              &s_cache_sd_text,
                                              "Offline (/sd unavailable)");
            s_cache_sd_state = 0;
            s_cache_sd_free_mib = UINT32_MAX;
            s_cache_sd_total_mib = UINT32_MAX;
        }
        ui_settings_obj_set_text_color_cached(s_widgets.sd_status, &s_cache_sd_color, COL_RED);
        return;
    }

    uint32_t free_mib = (uint32_t)(status.free_bytes / (1024ull * 1024ull));
    uint32_t total_mib = (uint32_t)(status.total_bytes / (1024ull * 1024ull));
    if (s_cache_sd_state != 1 ||
        s_cache_sd_free_mib != free_mib ||
        s_cache_sd_total_mib != total_mib) {
        char free_buf[24];
        char total_buf[24];
        char text[80];
        ui_settings_format_storage_size(status.free_bytes, free_buf, sizeof(free_buf));
        ui_settings_format_storage_size(status.total_bytes, total_buf, sizeof(total_buf));
        snprintf(text, sizeof(text), "Mounted: %s free / %s", free_buf, total_buf);
        ui_settings_label_set_text_cached(s_widgets.sd_status, &s_cache_sd_text, text);
        s_cache_sd_state = 1;
        s_cache_sd_free_mib = free_mib;
        s_cache_sd_total_mib = total_mib;
    }
    ui_settings_obj_set_text_color_cached(s_widgets.sd_status, &s_cache_sd_color, COL_GREEN);
}

static void ui_settings_update_s3_firmware_label(void)
{
    if (!s_label_s3_firmware) return;

    ctrl_firmware_report_t report;
    if (!control_link_get_s3_firmware_report(&report)) {
        ui_settings_label_set_text_cached(s_label_s3_firmware,
                                          &s_cache_s3_firmware_text,
                                          "S3: waiting for firmware report");
        ui_settings_obj_set_text_color_cached(s_label_s3_firmware,
                                               &s_cache_s3_firmware_color,
                                               COL_TEXT_DIM);
        return;
    }

    char text[80];
    snprintf(text, sizeof(text), "S3: %s [%s/%s]",
             report.version[0] ? report.version : "unknown",
             ui_settings_firmware_slot_label(report.slot),
             ui_settings_firmware_state_label(report.state));
    ui_settings_label_set_text_cached(s_label_s3_firmware,
                                      &s_cache_s3_firmware_text, text);
    lv_color_t color = report.state == CTRL_FW_STATE_VALID ? COL_GREEN :
                       report.state == CTRL_FW_STATE_PENDING_VERIFY ? COL_AMBER :
                       (report.state == CTRL_FW_STATE_INVALID ||
                        report.state == CTRL_FW_STATE_ABORTED) ? COL_RED : COL_TEXT_DIM;
    ui_settings_obj_set_text_color_cached(s_label_s3_firmware,
                                           &s_cache_s3_firmware_color, color);
}

#endif

void ui_settings_update(const ui_frame_context_t *ctx)
{
    if (!ctx || !ui_settings_is_active_tab(ctx->active_tab, s_config.settings_tab_index)) {
        return;
    }
#ifndef WIN32
    ui_settings_update_uart_status_label(&ctx->deck_state[CTRL_DECK_1]);
    ui_settings_update_sd_status_label(false);
    ui_settings_update_s3_firmware_label();
    ui_settings_apply_s3_debug_ap_status();
#if CONFIG_AUDIO_RECORDER_ENABLED
    ui_settings_update_recording_label();
#endif
    ui_settings_update_service_log_label();
#endif
}

void ui_settings_refresh_storage(void)
{
#ifndef WIN32
    ui_settings_update_sd_status_label(true);
#endif
}

#endif
