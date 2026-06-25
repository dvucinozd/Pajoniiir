#include "ui_settings.h"

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

#ifndef UI_SETTINGS_HOST_TEST

#include "esp_log.h"
#include "ui_theme.h"

#ifndef WIN32
#include "app_settings.h"
#include "bsp_jc4880.h"
#include "esp_timer.h"
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
static lv_obj_t *s_label_audio_out = NULL;
static lv_obj_t *s_label_cue_mode = NULL;
static ui_settings_color_cache_t s_cache_uart_color;
static ui_settings_color_cache_t s_cache_sd_color;
static int s_cache_uart_state = -1;
static uint32_t s_cache_uart_age_bucket = UINT32_MAX;
static int s_cache_sd_state = -1;
static uint32_t s_cache_sd_free_mib = UINT32_MAX;
static uint32_t s_cache_sd_total_mib = UINT32_MAX;
static uint32_t s_cache_sd_last_poll_ms = 0;
static ui_settings_text_cache_t s_cache_sd_text;

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

static void audio_out_event_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    bool rca = lv_obj_has_state(sw, LV_STATE_CHECKED);
#ifndef WIN32
    bsp_audio_set_output(rca ? BSP_AUDIO_OUT_RCA : BSP_AUDIO_OUT_SPEAKER);
    app_settings_set_audio_out(rca ? 1 : 0);
#endif
    if (s_label_audio_out) {
        lv_label_set_text(s_label_audio_out, rca ? "RCA LINE-OUT" : "SPEAKER");
    }
    ESP_LOGI(TAG, "Audio output: %s", rca ? "RCA line-out" : "Speaker");
}

#ifndef WIN32
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
    bool rca_init = (cfg.audio_out != 0);
#else
    int bl_init = 80;
    bool rca_init = false;
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

    lv_obj_t *audio_section = ui_settings_section(screen, left_x, 118, left_w, 86, "AUDIO OUTPUT");
    lv_obj_t *sw_audio = lv_switch_create(audio_section);
    lv_obj_set_pos(sw_audio, 16, 42);
    lv_obj_add_event_cb(sw_audio, audio_out_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_audio_out = ui_settings_value_label(audio_section,
                                                rca_init ? "RCA LINE-OUT" : "SPEAKER",
                                                COL_GREEN,
                                                &lv_font_montserrat_16,
                                                104,
                                                44);
    if (rca_init) {
        lv_obj_add_state(sw_audio, LV_STATE_CHECKED);
    }

    // CDJ Link UI elements have been removed as per user request

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

    ui_settings_value_label(status_section, "Board: JC4880P443C_I_W (ESP32-P4 N16R8)",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 146);
    ui_settings_value_label(status_section, "Firmware: Main Deck Engine v1.0.0-Beta (IDF v5.5)",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 168);

    lv_obj_t *mixer_section = ui_settings_section(screen, 30, 346, 740, 70, "MIXER / PFL ROUTING");
    ui_settings_static_tile(mixer_section, 18, 30, 108, 28,
                            "CH1 FADER", COL_ACCENT, COL_PANEL_DK, COL_ACCENT);
    ui_settings_static_tile(mixer_section, 138, 30, 108, 28,
                            "CH2 FADER", COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_settings_static_tile(mixer_section, 258, 30, 118, 28,
                            "CROSSFADER", COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
    ui_settings_static_tile(mixer_section, 388, 30, 88, 28,
                            "PFL D1", COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_settings_static_tile(mixer_section, 488, 30, 88, 28,
                            "PFL D2", COL_AMBER, COL_PANEL_DK, COL_AMBER);

    lv_obj_t *btn_cue = lv_button_create(mixer_section);
    lv_obj_remove_style_all(btn_cue);
    if (s_config.btn_secondary) {
        lv_obj_add_style(btn_cue, s_config.btn_secondary, LV_PART_MAIN);
    }
    if (s_config.pressed) {
        lv_obj_add_style(btn_cue, s_config.pressed, LV_STATE_PRESSED);
    }
    lv_obj_set_size(btn_cue, 124, 28);
    lv_obj_set_pos(btn_cue, 588, 30);
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

#endif

void ui_settings_update(const ui_frame_context_t *ctx)
{
    if (!ctx || ctx->active_tab != 6) {
        return;
    }
#ifndef WIN32
    ui_settings_update_uart_status_label(&ctx->deck_state[CTRL_DECK_1]);
    ui_settings_update_sd_status_label(false);
#endif
}

void ui_settings_refresh_storage(void)
{
#ifndef WIN32
    ui_settings_update_sd_status_label(true);
#endif
}

#endif
