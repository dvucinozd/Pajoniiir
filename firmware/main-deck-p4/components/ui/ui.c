#include "ui.h"
#include "lvgl.h"
#include "ui_theme.h"   // centralised colour palette (COL_*); needs lvgl.h above
#include "esp_log.h"
#include "deck_core.h"
#include "library.h"
#include "ui_active_deck_leds.h"
#include "ui_beat_indicator.h"
#include "ui_controls.h"
#include "ui_deck_anlz_store.h"
#include "ui_diagnostics.h"
#include "ui_frame_context.h"
#include "ui_library.h"
#include "ui_overview.h"
#include "ui_lvgl_backend.h"
#include "ui_overview_perf.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32
// ── Firmware-only: LVGL ↔ MIPI-DSI panel plumbing ────────────────────────────
#include "bsp_jc4880.h"
#include "audio_engine.h"
#include "app_settings.h"
#include "control_link.h"
#include "media_catalog.h"
#include "cdj_link_client.h"
#include "remote_cache.h"
#include "sd_diag_log.h"
#include "wifi_link.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// The UI canvas is 800x480 landscape; the physical ST7701 panel is 480x800
// portrait. LVGL renders landscape dirty rectangles, then the flush callback
// uses the ESP32-P4 PPA (Pixel Processing Accelerator) to rotate each rectangle
// into the panel's MIPI-DSI frame buffer. LVGL's own software rotation is
// unusable here, so we rotate in hardware.
#define UI_HOR_RES   800   // logical landscape width  (LVGL canvas)
#define UI_VER_RES   480   // logical landscape height
#define UI_TOPBAR_H   46
#define UI_CONTENT_Y  UI_TOPBAR_H
#define UI_CONTENT_H  (UI_VER_RES - UI_TOPBAR_H)
#endif

#ifndef UI_HOR_RES
#define UI_HOR_RES   800
#define UI_VER_RES   480
#define UI_TOPBAR_H   46
#define UI_CONTENT_Y  UI_TOPBAR_H
#define UI_CONTENT_H  (UI_VER_RES - UI_TOPBAR_H)
#endif

static const char *TAG = "ui";

// ─── UI State and Variables ──────────────────────────────────────────────────
static lv_obj_t *s_root_container = NULL;
static lv_obj_t *s_header_container = NULL;
static lv_obj_t *s_footer_container = NULL;
static lv_obj_t *s_screens[7];
static int       s_active_tab = 0;

// Header elements
static lv_obj_t *s_label_title = NULL;
static lv_obj_t *s_label_artist = NULL;
static lv_obj_t *s_label_time = NULL;          // elapsed (current position)
static lv_obj_t *s_label_time_remain = NULL;   // remaining until end of track
static lv_obj_t *s_label_bpm = NULL;
static lv_obj_t *s_label_pitch = NULL;
static lv_obj_t *s_label_status_indicator = NULL;
static uint32_t  s_status_override_until_ms = 0;

// Sub-screen elements
static ui_deck_track_info_t s_deck_track_info[DECK_CORE_DECK_COUNT];
static ui_deck_anlz_store_t s_deck_anlz_store;
static ui_controls_state_t s_controls;

#ifndef WIN32
#endif

// UI update timing diagnostics
static ui_overview_perf_counter_t s_ui_update_interval_perf;
static ui_overview_perf_counter_t s_ui_update_duration_perf;

// Performance tab widgets
static lv_obj_t *s_hot_cue_buttons[8];
static lv_obj_t *s_loop_buttons[6];
static lv_obj_t *s_label_loop_status = NULL;

// Footer navigation buttons
static lv_obj_t *s_footer_buttons[7];
static lv_obj_t *s_footer_active_strips[7];
static const char *s_tab_names[7] = {
    "OVERVIEW", "LIBRARY", "HOT CUES", "LOOP", "BEAT JUMP", "KEY SHIFT", "SETTINGS"
};

// Settings Screen Widgets
static lv_obj_t *s_slider_backlight = NULL;
static lv_obj_t *s_label_brightness_val = NULL;
static lv_obj_t *s_label_uart_status = NULL;
static lv_obj_t *s_label_audio_out = NULL;
static lv_obj_t *s_label_link_status = NULL;
static lv_obj_t *s_label_link_mode = NULL;
static lv_obj_t *s_label_sd_status = NULL;
static lv_obj_t *s_label_sd_cache_status = NULL;

typedef struct {
    bool valid;
    char text[80];
} ui_text_cache_t;

typedef struct {
    bool valid;
    uint32_t color;
} ui_color_cache_t;

static ui_text_cache_t s_cache_status_text;
static ui_text_cache_t s_cache_header_title;
static ui_text_cache_t s_cache_header_artist;
static ui_color_cache_t s_cache_status_color;
static ui_color_cache_t s_cache_uart_color;
static ui_color_cache_t s_cache_sd_color;
static int s_cache_pitch_centipct = INT_MIN;
static int s_cache_bpm_centi = INT_MIN;
static uint32_t s_cache_elapsed_centis = UINT32_MAX;
static uint32_t s_cache_remain_centis = UINT32_MAX;
static bool s_cache_time_loading = false;
static ui_color_cache_t s_cache_remain_color;
static int s_cache_uart_state = -1;
static uint32_t s_cache_uart_age_bucket = UINT32_MAX;
static int s_cache_sd_state = -1;
static uint32_t s_cache_sd_free_mib = UINT32_MAX;
static uint32_t s_cache_sd_total_mib = UINT32_MAX;
static uint32_t s_cache_sd_last_poll_ms = 0;
static ui_text_cache_t s_cache_sd_text;
static ui_text_cache_t s_cache_sd_cache_text;
static uint32_t s_cache_sd_cache_last_poll_ms = 0;
static uint32_t s_cache_sd_cache_mib = UINT32_MAX;
static uint32_t s_cache_sd_cache_tracks = UINT32_MAX;
static uint32_t s_cache_sd_cache_files = UINT32_MAX;

static void ui_invalidate_header_cache(void)
{
    s_cache_header_title.valid = false;
    s_cache_header_artist.valid = false;
    s_cache_pitch_centipct = INT_MIN;
    s_cache_bpm_centi = INT_MIN;
    s_cache_elapsed_centis = UINT32_MAX;
    s_cache_remain_centis = UINT32_MAX;
    s_cache_time_loading = false;
}

// ─── Style Definitions (Harmonious Dark Theme) ───────────────────────────────
static lv_style_t s_style_root;
static lv_style_t s_style_header;
static lv_style_t s_style_footer;
static lv_style_t s_style_tab_btn_normal;
static lv_style_t s_style_tab_btn_active;
static lv_style_t s_style_tab_btn_disabled;
static lv_style_t s_style_screen_bg;
static lv_style_t s_style_panel_frame;
static lv_style_t s_style_btn_primary;
static lv_style_t s_style_btn_amber;
static lv_style_t s_style_btn_secondary;
static lv_style_t s_style_btn_disabled;
static lv_style_t s_style_btn_neon;
static lv_style_t s_style_pressed;   // color-agnostic touch feedback (dim on press)

#ifndef WIN32
static void ui_update_sd_status_label(bool force);
static void ui_update_sd_cache_status_label(bool force);
#endif
static void ui_update_hot_cues(void);
static void ui_refresh_loop_screen_from_target(void);
static void ui_set_performance_deck(uint8_t deck);
static void jump_btn_event_cb(lv_event_t *e);
static void ui_load_waveform_data(uint8_t deck,
                                  uint32_t duration_ms,
                                  const uint8_t waveform_low[400],
                                  bool has_waveform,
                                  const anlz_metadata_t *meta);
static void ui_cache_invalidate(void)
{
    s_cache_status_text.valid = false;
    s_cache_status_color.valid = false;
    s_cache_uart_color.valid = false;
    s_cache_sd_color.valid = false;
    s_cache_pitch_centipct = INT_MIN;
    s_cache_bpm_centi = INT_MIN;
    s_cache_elapsed_centis = UINT32_MAX;
    s_cache_remain_centis = UINT32_MAX;
    s_cache_time_loading = false;
    s_cache_remain_color.valid = false;
    s_cache_uart_state = -1;
    s_cache_uart_age_bucket = UINT32_MAX;
    s_cache_sd_state = -1;
    s_cache_sd_free_mib = UINT32_MAX;
    s_cache_sd_total_mib = UINT32_MAX;
    s_cache_sd_last_poll_ms = 0;
    s_cache_sd_text.valid = false;
    s_cache_sd_cache_text.valid = false;
    s_cache_sd_cache_last_poll_ms = 0;
    s_cache_sd_cache_mib = UINT32_MAX;
    s_cache_sd_cache_tracks = UINT32_MAX;
    s_cache_sd_cache_files = UINT32_MAX;
}

static void ui_label_set_text_cached(lv_obj_t *label, ui_text_cache_t *cache, const char *text)
{
    if (!label || !cache) return;
    const char *safe_text = text ? text : "";
    if (cache->valid && strncmp(cache->text, safe_text, sizeof(cache->text)) == 0) {
        return;
    }
    lv_label_set_text(label, safe_text);
    snprintf(cache->text, sizeof(cache->text), "%s", safe_text);
    cache->valid = true;
}

static void ui_obj_set_text_color_cached(lv_obj_t *obj, ui_color_cache_t *cache, lv_color_t color)
{
    if (!obj || !cache) return;
    uint32_t color_u32 = lv_color_to_u32(color);
    if (cache->valid && cache->color == color_u32) {
        return;
    }
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    cache->color = color_u32;
    cache->valid = true;
}

// LVGL's builtin vsnprintf has NO %f support unless LV_USE_FLOAT is enabled
// (lv_sprintf_builtin.c: PRINTF_SUPPORT_FLOAT gates on it). Enabling LV_USE_FLOAT
// would also switch lv_value_precise_t to float across all transform/anim math,
// which we don't want. So render fixed 2-decimal floats here via integer math —
// works on firmware and the PC simulator regardless of the sprintf config.
static void ui_label_set_f2(lv_obj_t *lbl, float v) {
    if (!lbl) return;
    int c = (int)(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f));
    if (c < 0) lv_label_set_text_fmt(lbl, "-%d.%02d", (-c) / 100, (-c) % 100);
    else       lv_label_set_text_fmt(lbl, "%d.%02d", c / 100, c % 100);
}

static void ui_format_time_cc(char *out, size_t out_sz, uint32_t ms)
{
    uint32_t secs = ms / 1000;
    uint32_t centis = (ms % 1000) / 10;
    snprintf(out, out_sz, "%02u:%02u.%02u",
             (unsigned)(secs / 60),
             (unsigned)(secs % 60),
             (unsigned)centis);
}

static void ui_label_set_small_caps(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

static void ui_status_indicator_set(const char *text, lv_color_t color)
{
    if (!s_label_status_indicator) {
        return;
    }
    ui_label_set_text_cached(s_label_status_indicator, &s_cache_status_text, text ? text : "LOAD ERR");
    ui_obj_set_text_color_cached(s_label_status_indicator, &s_cache_status_color, color);
}

static void ui_status_indicator_hold(const char *text, lv_color_t color, uint32_t hold_ms)
{
    s_status_override_until_ms = lv_tick_get() + hold_ms;
    ui_status_indicator_set(text, color);
}

static bool ui_status_indicator_has_override(void)
{
    return (int32_t)(s_status_override_until_ms - lv_tick_get()) > 0;
}

static lv_color_t ui_status_color_for_text(const char *status)
{
    if (!status || status[0] == '\0') {
        return COL_RED;
    }
    if (strcmp(status, "HOST BUSY") == 0) {
        return COL_AMBER;
    }
    if (strcmp(status, "JOIN OFFLINE") == 0 ||
        strcmp(status, "JOIN FAILED") == 0 ||
        strcmp(status, "MANIFEST ERR") == 0 ||
        strcmp(status, "DAT ERR") == 0 ||
        strcmp(status, "AUDIO ERR") == 0 ||
        strcmp(status, "TASK CREATE ERR") == 0 ||
        strcmp(status, "STOP ERR") == 0 ||
        strcmp(status, "NO MEM") == 0 ||
        strcmp(status, "NO AUDIO FRAME") == 0 ||
        strcmp(status, "CODEC OPEN ERR") == 0 ||
        strcmp(status, "NOT FOUND") == 0 ||
        strcmp(status, "LOAD ERR") == 0) {
        return COL_RED;
    }
    if (strcmp(status, "JOINED") == 0 ||
        strcmp(status, "CACHE READY") == 0 ||
        strcmp(status, "TRACK LOADED") == 0) {
        return COL_GREEN;
    }
    if (strcmp(status, "LOADING") == 0 ||
        strcmp(status, "CACHE START") == 0 ||
        strcmp(status, "MANIFEST") == 0 ||
        strcmp(status, "ANLZ0000.DAT") == 0 ||
        strcmp(status, "ANLZ0000.EXT") == 0 ||
        strcmp(status, "audio.mp3") == 0) {
        return COL_ACCENT;
    }
    return COL_TEXT_DIM;
}

static void ui_style_hot_cue_pad(int index, bool is_loop, bool is_empty)
{
    if (index < 0 || index >= 8 || !s_hot_cue_buttons[index]) {
        return;
    }

    static const uint32_t cue_hex_colors[8] = {
        0x00E676,  // A: Green
        0x00E5FF,  // B: Cyan
        0xFFAB00,  // C: Orange/Amber
        0xE040FB,  // D: Pink
        0xFFD600,  // E: Yellow
        0xFF1744,  // F: Red
        0x7C4DFF,  // G: Purple
        0x2979FF   // H: Blue
    };

    lv_obj_t *btn = s_hot_cue_buttons[index];
    lv_color_t pad_color = lv_color_hex(cue_hex_colors[index]);
    lv_color_t accent = is_empty ? COL_BORDER_LT : pad_color;
    lv_color_t bg = is_empty ? COL_PANEL_DK : accent;
    lv_color_t text = is_empty ? COL_TEXT_DIM : accent;

    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, is_empty ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, is_empty ? 1 : 2, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_t *lbl_pad = lv_obj_get_child(btn, 0);
    if (lbl_pad) {
        lv_obj_set_style_text_color(lbl_pad, text, LV_PART_MAIN);
    }

    lv_obj_t *lbl_time = lv_obj_get_child(btn, 1);
    if (lbl_time) {
        lv_obj_set_style_text_color(lbl_time, is_empty ? COL_TEXT_DIM : COL_TEXT, LV_PART_MAIN);
    }
}

static void ui_update_loop_screen_state(void)
{
    ui_controls_loop_state_t loop = ui_controls_active_loop(&s_controls);

    if (s_label_loop_status) {
        if (loop.active && loop.beats > 0) {
            lv_label_set_text_fmt(s_label_loop_status, "ACTIVE: %d BEATS", loop.beats);
            lv_obj_set_style_text_color(s_label_loop_status, COL_RED, LV_PART_MAIN);
        } else if (loop.active) {
            lv_label_set_text(s_label_loop_status, "ACTIVE LOOP");
            lv_obj_set_style_text_color(s_label_loop_status, COL_RED, LV_PART_MAIN);
        } else {
            lv_label_set_text(s_label_loop_status, "NO ACTIVE LOOP");
            lv_obj_set_style_text_color(s_label_loop_status, COL_TEXT_DIM, LV_PART_MAIN);
        }
    }

    static const int loop_beats[6] = {1, 2, 4, 8, 16, 32};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *btn = s_loop_buttons[i];
        if (!btn) {
            continue;
        }

        bool is_active = loop.active && loop.beats == loop_beats[i];
        lv_color_t accent = is_active ? COL_RED : lv_color_hex(0x123A1B);
        lv_obj_set_style_bg_color(btn, is_active ? COL_RED : lv_color_hex(0x051A0B), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, is_active ? LV_OPA_30 : LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, accent, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, is_active ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, is_active ? COL_RED : COL_TEXT_MUTED, LV_PART_MAIN);
        }
    }
}

static lv_obj_t *ui_settings_section(lv_obj_t *parent, int x, int y, int w, int h, const char *title)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_add_style(section, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(section, w, h);
    lv_obj_set_pos(section, x, y);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(section);
    ui_label_set_small_caps(label, title, COL_TEXT_MUTED);
    lv_obj_set_pos(label, 14, 12);

    return section;
}

static lv_obj_t *ui_settings_value_label(lv_obj_t *parent, const char *text, lv_color_t color,
                                         const lv_font_t *font, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *ui_static_tile(lv_obj_t *parent, int x, int y, int w, int h,
                                const char *text, lv_color_t text_color,
                                lv_color_t fill_color, lv_color_t border_color)
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

static lv_obj_t *ui_create_beat_jump_button(lv_obj_t *parent, int x, int y, int value,
                                            bool forward)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 150, 82);
    lv_obj_set_pos(btn, x, y);

    lv_color_t accent = forward ? COL_GREEN : COL_RED;
    lv_color_t fill = forward ? lv_color_hex(0x10251B) : lv_color_hex(0x2A1016);
    lv_obj_set_style_bg_color(btn, fill, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_set_user_data(btn, (void*)(intptr_t)value);
    lv_obj_add_event_cb(btn, jump_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    int abs_value = value < 0 ? -value : value;
    lv_label_set_text_fmt(lbl, "%c%d BEAT%s", forward ? '+' : '-', abs_value,
                          abs_value == 1 ? "" : "S");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}

static uint8_t ui_deck_index(uint8_t deck)
{
    return deck < DECK_CORE_DECK_COUNT ? deck : DECK_CORE_COMPAT_DECK;
}

static void ui_deck_track_info_clear(uint8_t deck)
{
    uint8_t idx = ui_deck_index(deck);
    memset(&s_deck_track_info[idx], 0, sizeof(s_deck_track_info[idx]));
}

static void ui_deck_track_info_set(uint8_t deck,
                                   const char *title,
                                   const char *artist,
                                   uint16_t bpm,
                                   uint32_t duration_ms)
{
    uint8_t idx = ui_deck_index(deck);
    ui_deck_track_info_t *info = &s_deck_track_info[idx];
    memset(info, 0, sizeof(*info));
    snprintf(info->title, sizeof(info->title), "%s",
             title && title[0] ? title : "Unknown Title");
    snprintf(info->artist, sizeof(info->artist), "%s",
             artist && artist[0] ? artist : "Unknown Artist");
    info->bpm = bpm;
    info->duration_ms = duration_ms;
    info->valid = true;
}

static uint32_t ui_deck_duration_ms(uint8_t deck)
{
    uint32_t fallback = 0;
    if (deck == CTRL_DECK_1) {
        const library_track_t *track = library_get_ptr(mock_library_get_current_track_index());
        fallback = track ? track->duration_ms : 0;
    }
    return ui_library_deck_duration_ms(deck, fallback);
}

static uint16_t ui_deck_bpm(uint8_t deck)
{
    uint16_t fallback = 120;
    if (deck == CTRL_DECK_1) {
        const library_track_t *track = library_get_ptr(mock_library_get_current_track_index());
        fallback = track ? track->bpm : 120;
    }
    return ui_library_deck_bpm(deck, fallback);
}

static uint16_t ui_performance_bpm(void)
{
    return ui_deck_bpm(ui_controls_active_deck(&s_controls));
}

static const anlz_metadata_t *ui_deck_anlz(uint8_t deck)
{
    uint8_t idx = ui_deck_index(deck);
    const anlz_metadata_t *meta = ui_deck_anlz_store_get(&s_deck_anlz_store, idx);
    if (meta) {
        return meta;
    }

    if (deck != CTRL_DECK_1) return NULL;
    return library_get_current_anlz();
}

static const anlz_metadata_t *ui_performance_anlz(void)
{
    return ui_deck_anlz(ui_controls_active_deck(&s_controls));
}

static deck_state_t ui_performance_deck_state(void)
{
    uint8_t deck = ui_controls_active_deck(&s_controls);
    return deck == CTRL_DECK_1 ? deck_core_get_state()
                               : deck_core_get_deck_state(deck);
}

static void ui_update_active_header_track(uint8_t deck)
{
    uint8_t idx = ui_deck_index(deck);
    const ui_deck_track_info_t *info = &s_deck_track_info[idx];

    char title[128];
    snprintf(title, sizeof(title), "D%u  %s",
             (unsigned)idx + 1u,
             (info->valid && info->title[0]) ? info->title : "No Track");
    ui_label_set_text_cached(s_label_title, &s_cache_header_title, title);

    ui_label_set_text_cached(s_label_artist,
                             &s_cache_header_artist,
                             (info->valid && info->artist[0]) ? info->artist : "");
}

static void ui_set_loop_shadow(uint8_t deck,
                               bool active,
                               uint32_t start_ms,
                               uint32_t end_ms,
                               int beats)
{
    uint8_t idx = ui_deck_index(deck);
    ui_controls_set_loop_shadow(&s_controls, idx, active, start_ms, end_ms, beats);

    if (ui_controls_is_active_deck(&s_controls, idx)) {
        ui_update_loop_screen_state();
    }
}

static void ui_refresh_loop_screen_from_target(void)
{
    ui_update_loop_screen_state();
}

static void ui_set_performance_deck(uint8_t deck)
{
    uint8_t before = ui_controls_active_deck(&s_controls);
    ui_controls_set_active_deck(&s_controls, ui_deck_index(deck));
    uint8_t after = ui_controls_active_deck(&s_controls);
    if (before != after) {
        ui_invalidate_header_cache();
    }

    ui_controls_update_performance_target_visuals(&s_controls);
    ui_refresh_loop_screen_from_target();
    ui_update_hot_cues();

    if (before != after) {
        ui_status_indicator_hold(after == CTRL_DECK_1 ? "TARGET D1" : "TARGET D2",
                                 after == CTRL_DECK_1 ? COL_ACCENT : COL_GREEN,
                                 1200);
    }
}

static void ui_deck_anlz_set_from_current(uint8_t deck, const anlz_metadata_t *meta)
{
    uint8_t idx = ui_deck_index(deck);
    if (!meta || !ui_deck_anlz_store_set(&s_deck_anlz_store, idx, meta)) {
        ui_deck_anlz_store_clear(&s_deck_anlz_store, idx);
        ESP_LOGW(TAG, "Deck %u ANLZ metadata unavailable", (unsigned)idx + 1u);
    }
}

#ifndef WIN32
static void ui_update_uart_status_label(const deck_state_t *state)
{
    if (!s_label_uart_status || !state) {
        return;
    }

    int display_state;
    uint32_t age_bucket;
    if (state->control_link_connected) {
        display_state = 1;
        if (state->last_heartbeat_age_ms < 1000u) {
            age_bucket = state->last_heartbeat_age_ms / 100u;
            if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
                lv_label_set_text_fmt(s_label_uart_status,
                                      "Control Link (S3): Connected (age %lu ms)",
                                      (unsigned long)(age_bucket * 100u));
                s_cache_uart_state = display_state;
                s_cache_uart_age_bucket = age_bucket;
            }
        } else {
            age_bucket = state->last_heartbeat_age_ms / 1000u;
            if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
                lv_label_set_text_fmt(s_label_uart_status,
                                      "Control Link (S3): Connected (age %lu s)",
                                      (unsigned long)age_bucket);
                s_cache_uart_state = display_state;
                s_cache_uart_age_bucket = age_bucket;
            }
        }
        ui_obj_set_text_color_cached(s_label_uart_status, &s_cache_uart_color, COL_GREEN);
        return;
    }

    if (state->last_heartbeat_age_ms == UINT32_MAX) {
        display_state = 0;
        age_bucket = UINT32_MAX;
        if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
            lv_label_set_text(s_label_uart_status, "Control Link (S3): Offline (no heartbeat)");
            s_cache_uart_state = display_state;
            s_cache_uart_age_bucket = age_bucket;
        }
    } else {
        display_state = 2;
        age_bucket = state->last_heartbeat_age_ms / 1000u;
        if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
            lv_label_set_text_fmt(s_label_uart_status,
                                  "Control Link (S3): Offline (last %lu s ago)",
                                  (unsigned long)age_bucket);
            s_cache_uart_state = display_state;
            s_cache_uart_age_bucket = age_bucket;
        }
    }
    ui_obj_set_text_color_cached(s_label_uart_status, &s_cache_uart_color, COL_RED);
}

static void ui_format_storage_size(uint64_t bytes, char *out, size_t out_size)
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

static void ui_update_sd_status_label(bool force)
{
    if (!s_label_sd_status) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ull);
    if (!force && s_cache_sd_last_poll_ms != 0 &&
        (uint32_t)(now_ms - s_cache_sd_last_poll_ms) < 1000u) {
        return;
    }
    s_cache_sd_last_poll_ms = now_ms;

    bsp_sd_status_t status;
    esp_err_t rc = bsp_sd_get_status(&status);
    if (rc != ESP_OK || !status.mounted) {
        if (s_cache_sd_state != 0) {
            ui_label_set_text_cached(s_label_sd_status, &s_cache_sd_text, "Offline (/sd unavailable)");
            s_cache_sd_state = 0;
            s_cache_sd_free_mib = UINT32_MAX;
            s_cache_sd_total_mib = UINT32_MAX;
        }
        ui_obj_set_text_color_cached(s_label_sd_status, &s_cache_sd_color, COL_RED);
        return;
    }

    uint32_t free_mib = (uint32_t)(status.free_bytes / (1024ull * 1024ull));
    uint32_t total_mib = (uint32_t)(status.total_bytes / (1024ull * 1024ull));
    if (s_cache_sd_state != 1 ||
        s_cache_sd_free_mib != free_mib ||
        s_cache_sd_total_mib != total_mib) {
        char free_buf[16];
        char total_buf[16];
        char text[80];
        ui_format_storage_size(status.free_bytes, free_buf, sizeof(free_buf));
        ui_format_storage_size(status.total_bytes, total_buf, sizeof(total_buf));
        snprintf(text, sizeof(text), "Mounted: %s free / %s", free_buf, total_buf);
        ui_label_set_text_cached(s_label_sd_status, &s_cache_sd_text, text);
        s_cache_sd_state = 1;
        s_cache_sd_free_mib = free_mib;
        s_cache_sd_total_mib = total_mib;
    }
    ui_obj_set_text_color_cached(s_label_sd_status, &s_cache_sd_color, COL_GREEN);
}

static void ui_update_sd_cache_status_label(bool force)
{
    if (!s_label_sd_cache_status) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ull);
    if (!force && s_cache_sd_cache_last_poll_ms != 0 &&
        (uint32_t)(now_ms - s_cache_sd_cache_last_poll_ms) < 1000u) {
        return;
    }
    s_cache_sd_cache_last_poll_ms = now_ms;

    remote_cache_stats_t stats;
    esp_err_t rc = remote_cache_get_stats(&stats);
    if (rc != ESP_OK) {
        ui_label_set_text_cached(s_label_sd_cache_status, &s_cache_sd_cache_text,
                                 "Cache unavailable");
        s_cache_sd_cache_mib = UINT32_MAX;
        s_cache_sd_cache_tracks = UINT32_MAX;
        s_cache_sd_cache_files = UINT32_MAX;
        return;
    }

    uint32_t mib = (uint32_t)(stats.bytes / (1024ull * 1024ull));
    if (s_cache_sd_cache_mib != mib ||
        s_cache_sd_cache_tracks != stats.tracks ||
        s_cache_sd_cache_files != stats.files) {
        char size_buf[16];
        char text[80];
        ui_format_storage_size(stats.bytes, size_buf, sizeof(size_buf));
        snprintf(text, sizeof(text), "%s, %lu tracks, %lu files",
                 size_buf,
                 (unsigned long)stats.tracks,
                 (unsigned long)stats.files);
        ui_label_set_text_cached(s_label_sd_cache_status, &s_cache_sd_cache_text, text);
        s_cache_sd_cache_mib = mib;
        s_cache_sd_cache_tracks = stats.tracks;
        s_cache_sd_cache_files = stats.files;
    }
}

static const char *ui_link_mode_name(uint8_t mode)
{
    switch (mode) {
    case WIFI_LINK_MODE_HOST:
        return "HOST USB";
    case WIFI_LINK_MODE_JOIN:
        return "JOIN PLAYER";
    default:
        return "OFF";
    }
}

static void ui_update_link_status_label(void)
{
    if (!s_label_link_status) {
        return;
    }

    wifi_link_status_t st = wifi_link_get_status();
    if (st.mode == WIFI_LINK_MODE_HOST) {
        lv_label_set_text_fmt(s_label_link_status, "Link: HOST %s (%u client)",
                              st.ssid[0] ? st.ssid : "CDJ100S",
                              (unsigned)st.ap_clients);
        return;
    }

    if (st.mode == WIFI_LINK_MODE_JOIN) {
        cdj_link_peer_t peer;
        if (cdj_link_client_get_peer(&peer)) {
            lv_label_set_text_fmt(s_label_link_status, "Link: JOINED %s (%lu tracks)",
                                  peer.name[0] ? peer.name : peer.host,
                                  (unsigned long)peer.track_count);
        } else {
            lv_label_set_text(s_label_link_status, "Link: JOIN SCANNING");
        }
        return;
    }

    lv_label_set_text(s_label_link_status, "Link: OFF");
}
#endif

// ─── Event Callbacks ─────────────────────────────────────────────────────────

// Switch screens when a footer button is tapped
static void footer_btn_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int target_idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    // Update visibility of screens
    for (int i = 0; i < 7; i++) {
        if (i == target_idx) {
            lv_obj_remove_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_style(s_footer_buttons[i], &s_style_tab_btn_active, LV_PART_MAIN);
            if (s_footer_active_strips[i]) {
                lv_obj_remove_flag(s_footer_active_strips[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_replace_style(s_footer_buttons[i], &s_style_tab_btn_active,
                                 &s_style_tab_btn_normal, LV_PART_MAIN);
            if (s_footer_active_strips[i]) {
                lv_obj_add_flag(s_footer_active_strips[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    s_active_tab = target_idx;
    ESP_LOGD(TAG, "Switched to tab %d (%s)", target_idx, s_tab_names[target_idx]);
}

static uint8_t ui_deck_control_id(uint8_t deck, uint8_t deck1_id, uint8_t deck2_id)
{
    return ui_deck_index(deck) == CTRL_DECK_2 ? deck2_id : deck1_id;
}

static void ui_overview_action_play_pause(uint8_t deck)
{
#ifdef WIN32
    (void)deck;
    mock_deck_toggle_play();
    deck_state_t state = deck_core_get_state();
    ESP_LOGI(TAG, "Simulator Play/Pause: %s", state.playing ? "PLAYING" : "PAUSED");
#else
    ctrl_event_t ev = {
        .type  = CTRL_EV_BUTTON,
        .id    = ui_deck_control_id(deck, CTRL_ID_DECK1_PLAY, CTRL_ID_DECK2_PLAY),
        .deck  = deck,
        .value = 1,
        .seq   = 0
    };
    deck_core_queue_event(&ev);
#endif
}

static void ui_overview_action_cue(uint8_t deck)
{
#ifdef WIN32
    (void)deck;
    mock_deck_set_playing(false);
    mock_deck_set_position(0);
#else
    ctrl_event_t ev = {
        .type  = CTRL_EV_BUTTON,
        .id    = ui_deck_control_id(deck, CTRL_ID_DECK1_CUE, CTRL_ID_DECK2_CUE),
        .deck  = deck,
        .value = 1,
        .seq   = 0
    };
    deck_core_queue_event(&ev);
#endif
}

static void ui_overview_action_seek(uint8_t deck, uint32_t target_ms)
{
#ifndef WIN32
    audio_engine_deck_seek(deck, target_ms);
#else
    (void)deck;
    mock_deck_set_position(target_ms);
#endif
}

// Trigger hot cue pads
static void hot_cue_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int cue_idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    ui_controls_hot_cue_t cue = ui_controls_hot_cue(&s_controls, (uint8_t)cue_idx);
    uint32_t pos = cue.position_ms;
    uint8_t deck = ui_controls_active_deck(&s_controls);

    if (cue.empty || pos == UI_CONTROLS_EMPTY_HOT_CUE_MS) {
        ESP_LOGI(TAG, "D%u Hot Cue %c is empty, ignoring click",
                 (unsigned)deck + 1u, 'A' + cue_idx);
        return;
    }

    uint8_t type = cue.type;
    uint32_t end_pos = cue.end_ms;

    if (type == 2 && end_pos > pos) { // Hot Loop
        ui_set_loop_shadow(deck, true, pos, end_pos, 0);
#ifndef WIN32
        audio_engine_deck_seek(deck, pos);
        audio_engine_deck_set_loop(deck, pos, end_pos);
        audio_engine_deck_play(deck);
        ESP_LOGI(TAG, "D%u Hot Loop %c active: %lu - %lu ms on hardware",
                 (unsigned)deck + 1u, 'A' + cue_idx, (unsigned long)pos, (unsigned long)end_pos);
#else
        mock_deck_set_position(pos);
        mock_deck_set_playing(true);
        ESP_LOGI(TAG, "D%u Hot Loop %c active: %lu - %lu ms (simulated)",
                 (unsigned)deck + 1u, 'A' + cue_idx, (unsigned long)pos, (unsigned long)end_pos);
#endif
    } else { // Normal Cue
        ui_set_loop_shadow(deck, false, 0, 0, 0);
#ifndef WIN32
        audio_engine_deck_clear_loop(deck);
        audio_engine_deck_seek(deck, pos);
        audio_engine_deck_play(deck);
        ESP_LOGI(TAG, "D%u Hot Cue %c triggered at %lu ms on hardware",
                 (unsigned)deck + 1u, 'A' + cue_idx, (unsigned long)pos);
#else
        mock_deck_set_position(pos);
        mock_deck_set_playing(true);
        ESP_LOGI(TAG, "D%u Hot Cue %c triggered at %d ms",
                 (unsigned)deck + 1u, 'A' + cue_idx, pos);
#endif
    }
}

// Loop pad clicked (e.g. 1/2, 1, 2, 4, 8, 16 beats)
static void loop_btn_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int beats = (int)(intptr_t)lv_obj_get_user_data(btn);
    uint8_t deck = ui_controls_active_deck(&s_controls);

    float bpm = (float)ui_performance_bpm();
    if (bpm <= 0.0f) bpm = 120.0f;
    uint32_t beat_len_ms = (uint32_t)(60000.0f / bpm);

    uint32_t start_ms = 0;
#ifndef WIN32
    start_ms = audio_engine_deck_position_ms(deck);
#else
    deck_state_t state = ui_performance_deck_state();
    start_ms = state.position_ms;
#endif

    uint32_t end_ms = start_ms + (beat_len_ms * beats);
    ui_set_loop_shadow(deck, true, start_ms, end_ms, beats);

#ifndef WIN32
    audio_engine_deck_set_loop(deck, start_ms, end_ms);
    ESP_LOGI(TAG, "D%u Hardware Loop of %d beats active: %lu to %lu ms",
             (unsigned)deck + 1u, beats, (unsigned long)start_ms, (unsigned long)end_ms);
#else
    ESP_LOGI(TAG, "D%u Simulating Loop of %d beats: %d ms to %d ms",
             (unsigned)deck + 1u, beats, start_ms, end_ms);
#endif
}

// Loop exit button
static void exit_loop_event_cb(lv_event_t *e) {
    (void)e;
    uint8_t deck = ui_controls_active_deck(&s_controls);
    ui_set_loop_shadow(deck, false, 0, 0, 0);
#ifndef WIN32
    audio_engine_deck_clear_loop(deck);
    ESP_LOGI(TAG, "D%u Loop exited on hardware", (unsigned)deck + 1u);
#else
    ESP_LOGI(TAG, "D%u Loop exited", (unsigned)deck + 1u);
#endif
}

// Beat jump buttons clicked
static void jump_btn_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int val = (int)(intptr_t)lv_obj_get_user_data(btn);
    uint8_t deck = ui_controls_active_deck(&s_controls);

    deck_state_t state = ui_performance_deck_state();
    const anlz_metadata_t *meta = ui_performance_anlz();
    uint32_t new_pos = 0;

    if (meta && meta->beat_count > 0) {
        /* Find the closest beat in the PQTZ beatgrid */
        int closest_idx = 0;
        uint32_t min_diff = 0xFFFFFFFF;
        for (int i = 0; i < meta->beat_count; i++) {
            uint32_t diff = (state.position_ms > meta->beats[i].time_ms) ? 
                            (state.position_ms - meta->beats[i].time_ms) : 
                            (meta->beats[i].time_ms - state.position_ms);
            if (diff < min_diff) {
                min_diff = diff;
                closest_idx = i;
            }
        }
        
        int target_idx = closest_idx + val;
        if (target_idx < 0) target_idx = 0;
        if (target_idx >= meta->beat_count) target_idx = meta->beat_count - 1;
        
        new_pos = meta->beats[target_idx].time_ms;
        ESP_LOGI(TAG, "Beat Jump using beatgrid: from beat %d (%d ms) to beat %d (%d ms), shift=%d", 
                 closest_idx, state.position_ms, target_idx, new_pos, val);
    } else {
        /* Fallback to BPM-based calculation */
        float bpm = (float)ui_performance_bpm();
        if (bpm <= 0.0f) bpm = 120.0f;
        uint32_t beat_len_ms = (uint32_t)(60000.0f / bpm);
        
        int32_t shift = (int32_t)beat_len_ms * val;
        int32_t target_pos = (int32_t)state.position_ms + shift;
        if (target_pos < 0) target_pos = 0;
        new_pos = (uint32_t)target_pos;
        ESP_LOGI(TAG, "Beat Jump using BPM fallback: from %d ms to %d ms, shift=%d beats", 
                 state.position_ms, new_pos, val);
    }

#ifndef WIN32
    audio_engine_deck_seek(deck, new_pos);
#else
    mock_deck_set_position(new_pos);
#endif
    ui_set_loop_shadow(deck, false, 0, 0, 0);
}

// Key lock toggle
static void keylock_toggle_event_cb(lv_event_t *e) {
#ifdef WIN32
    mock_deck_toggle_master_tempo();
    deck_state_t state = deck_core_get_state();
    ESP_LOGI(TAG, "Master Tempo toggled: %s", state.master_tempo ? "ON" : "OFF");
#endif
}

// Screen brightness slider
static void slider_brightness_event_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    lv_label_set_text_fmt(s_label_brightness_val, "%d%%", val);
#ifndef WIN32
    bsp_display_set_backlight((uint8_t)val);   // live brightness
    app_settings_set_backlight((uint8_t)val);  // persist
#endif
    ESP_LOGI(TAG, "Backlight brightness set to %d%%", val);
}

// Audio output selector: switch OFF = onboard speaker, ON = RCA line-out
static void audio_out_event_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
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
static void link_mode_event_cb(lv_event_t *e)
{
    (void)e;
    app_settings_t cfg = app_settings_get();
    uint8_t next = (uint8_t)((cfg.link_mode + 1u) % 3u);
    app_settings_set_link_mode(next);
    if (s_label_link_mode) {
        lv_label_set_text_fmt(s_label_link_mode, "%s", ui_link_mode_name(next));
    }
    if (s_label_link_status) {
        lv_label_set_text(s_label_link_status, "Link mode saved; reboot applies Wi-Fi role");
    }
    ESP_LOGI(TAG, "Link mode saved: %s", ui_link_mode_name(next));
}

static void sd_cache_clear_event_cb(lv_event_t *e)
{
    (void)e;
    if (ui_library_has_remote_loaded_track()) {
        ui_status_indicator_hold("REMOTE LOADED", COL_AMBER, 2000);
        sd_diag_log_write("sd_cache", "clear blocked while remote track is loaded");
        return;
    }

    esp_err_t rc = remote_cache_clear();
    if (rc == ESP_OK) {
        ui_status_indicator_hold("CACHE CLEARED", COL_GREEN, 2000);
        sd_diag_log_write("sd_cache", "remote cache cleared");
    } else {
        ui_status_indicator_hold("CACHE ERR", COL_RED, 2000);
        sd_diag_log_write("sd_cache", "remote cache clear failed");
    }
    ui_cache_invalidate();
    ui_update_sd_status_label(true);
    ui_update_sd_cache_status_label(true);
}
#endif

// ─── Component Initialization Helpers ────────────────────────────────────────

static void init_styles(void) {
    // Root container style
    lv_style_init(&s_style_root);
    lv_style_set_bg_color(&s_style_root, COL_BG);
    lv_style_set_bg_opa(&s_style_root, LV_OPA_COVER);
    lv_style_set_pad_all(&s_style_root, 0);

    // Legacy header state sink. Hidden in the Pioneered layout but kept alive
    // because update paths still write active deck metadata into these labels.
    lv_style_init(&s_style_header);
    lv_style_set_bg_color(&s_style_header, COL_BG);
    lv_style_set_bg_opa(&s_style_header, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_style_header, 0);
    lv_style_set_border_color(&s_style_header, COL_BORDER);
    lv_style_set_border_side(&s_style_header, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_pad_left(&s_style_header, 0);
    lv_style_set_pad_right(&s_style_header, 0);

    // Top navigation bar style.
    lv_style_init(&s_style_footer);
    lv_style_set_bg_color(&s_style_footer, COL_FOOTER);
    lv_style_set_bg_opa(&s_style_footer, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_footer, 0);
    lv_style_set_border_color(&s_style_footer, COL_BORDER);
    lv_style_set_border_side(&s_style_footer, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_pad_all(&s_style_footer, 0);

    // Tab buttons - Pioneered normal
    lv_style_init(&s_style_tab_btn_normal);
    lv_style_set_bg_color(&s_style_tab_btn_normal, COL_BG);
    lv_style_set_bg_opa(&s_style_tab_btn_normal, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_normal, COL_TEXT_MUTED);
    lv_style_set_border_width(&s_style_tab_btn_normal, 1);
    lv_style_set_border_color(&s_style_tab_btn_normal, COL_BORDER_LT);
    lv_style_set_radius(&s_style_tab_btn_normal, 0);
    lv_style_set_pad_all(&s_style_tab_btn_normal, 0);
    
    // Tab buttons - Pioneered active
    lv_style_init(&s_style_tab_btn_active);
    lv_style_set_bg_color(&s_style_tab_btn_active, COL_BG);
    lv_style_set_bg_opa(&s_style_tab_btn_active, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_active, COL_TAB_ACTIVE);
    lv_style_set_border_width(&s_style_tab_btn_active, 2);
    lv_style_set_border_color(&s_style_tab_btn_active, COL_TAB_ACTIVE);
    lv_style_set_radius(&s_style_tab_btn_active, 0);
    lv_style_set_pad_all(&s_style_tab_btn_active, 0);

    // Tab buttons - Disabled (future use)
    lv_style_init(&s_style_tab_btn_disabled);
    lv_style_set_bg_color(&s_style_tab_btn_disabled, COL_SURFACE);
    lv_style_set_bg_opa(&s_style_tab_btn_disabled, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_disabled, COL_TEXT_DIM);
    lv_style_set_border_width(&s_style_tab_btn_disabled, 1);
    lv_style_set_border_color(&s_style_tab_btn_disabled, lv_color_hex(0x242424));
    lv_style_set_radius(&s_style_tab_btn_disabled, 0);
    lv_style_set_pad_all(&s_style_tab_btn_disabled, 0);

    // Sub-screen generic background
    lv_style_init(&s_style_screen_bg);
    lv_style_set_bg_color(&s_style_screen_bg, COL_BG);
    lv_style_set_bg_opa(&s_style_screen_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&s_style_screen_bg, 0);

    lv_style_init(&s_style_panel_frame);
    lv_style_set_bg_color(&s_style_panel_frame, COL_PANEL_DK);
    lv_style_set_bg_opa(&s_style_panel_frame, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_panel_frame, 1);
    lv_style_set_border_color(&s_style_panel_frame, COL_BORDER_LT);
    lv_style_set_radius(&s_style_panel_frame, 0);
    lv_style_set_pad_all(&s_style_panel_frame, 0);

    lv_style_init(&s_style_btn_primary);
    lv_style_set_bg_color(&s_style_btn_primary, COL_GREEN);
    lv_style_set_bg_opa(&s_style_btn_primary, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_primary, COL_ON_ACCENT);
    lv_style_set_border_width(&s_style_btn_primary, 1);
    lv_style_set_border_color(&s_style_btn_primary, lv_color_hex(0x6DFFB1));
    lv_style_set_radius(&s_style_btn_primary, 2);

    lv_style_init(&s_style_btn_amber);
    lv_style_set_bg_color(&s_style_btn_amber, COL_AMBER);
    lv_style_set_bg_opa(&s_style_btn_amber, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_amber, COL_ON_ACCENT);
    lv_style_set_border_width(&s_style_btn_amber, 1);
    lv_style_set_border_color(&s_style_btn_amber, lv_color_hex(0xFFD166));
    lv_style_set_radius(&s_style_btn_amber, 2);

    lv_style_init(&s_style_btn_secondary);
    lv_style_set_bg_color(&s_style_btn_secondary, COL_SURFACE);
    lv_style_set_bg_opa(&s_style_btn_secondary, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_secondary, COL_TEXT_MUTED);
    lv_style_set_border_width(&s_style_btn_secondary, 1);
    lv_style_set_border_color(&s_style_btn_secondary, COL_BORDER_LT);
    lv_style_set_radius(&s_style_btn_secondary, 2);

    lv_style_init(&s_style_btn_disabled);
    lv_style_set_bg_color(&s_style_btn_disabled, COL_DISABLED);
    lv_style_set_bg_opa(&s_style_btn_disabled, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_disabled, COL_TEXT_DIM);
    lv_style_set_border_width(&s_style_btn_disabled, 1);
    lv_style_set_border_color(&s_style_btn_disabled, COL_BORDER);
    lv_style_set_radius(&s_style_btn_disabled, 2);

    // Styled Neon Action Button
    lv_style_init(&s_style_btn_neon);
    lv_style_set_bg_color(&s_style_btn_neon, COL_GREEN);
    lv_style_set_bg_opa(&s_style_btn_neon, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_neon, COL_BG);
    lv_style_set_radius(&s_style_btn_neon, 2);

    // Universal touch feedback: dim + slightly shrink on press (works on any
    // colour). Attach with LV_STATE_PRESSED to interactive elements.
    lv_style_init(&s_style_pressed);
    lv_style_set_opa(&s_style_pressed, LV_OPA_70);
    lv_style_set_transform_width(&s_style_pressed, -3);
    lv_style_set_transform_height(&s_style_pressed, -3);
}

// Build the top bar UI elements
static void create_header(lv_obj_t *parent) {
    s_header_container = lv_obj_create(parent);
    lv_obj_remove_style_all(s_header_container);
    lv_obj_add_style(s_header_container, &s_style_header, LV_PART_MAIN);
    lv_obj_set_size(s_header_container, UI_HOR_RES, UI_TOPBAR_H);
    lv_obj_set_pos(s_header_container, 0, 0);
    lv_obj_add_flag(s_header_container, LV_OBJ_FLAG_HIDDEN);

    // Track Title (Left block)
    s_label_title = lv_label_create(s_header_container);
    lv_label_set_text(s_label_title, "Loading...");
    lv_obj_set_style_text_font(s_label_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_title, COL_TEXT, LV_PART_MAIN);
    lv_label_set_long_mode(s_label_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(s_label_title, 220, 24);
    lv_obj_set_pos(s_label_title, 10, 5);

    s_label_artist = lv_label_create(s_header_container);
    lv_label_set_text(s_label_artist, "No Track Loaded");
    lv_obj_set_style_text_font(s_label_artist, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_artist, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(s_label_artist, 10, 30);

    // Playhead indicator
    s_label_status_indicator = lv_label_create(s_header_container);
    ui_label_set_small_caps(s_label_status_indicator, "PAUSE", COL_AMBER);
    lv_obj_set_pos(s_label_status_indicator, 245, 18);

    // Elapsed time (current position) — large monospace, centred, blue.
    s_label_time = lv_label_create(s_header_container);
    lv_label_set_text(s_label_time, "00:00.00");
    lv_obj_set_style_text_font(s_label_time, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_time, COL_ACCENT, LV_PART_MAIN);
    lv_obj_align(s_label_time, LV_ALIGN_CENTER, 0, 0);

    // Remaining time (until end of track) — sits immediately to the right of the
    // elapsed counter, slightly smaller and dimmer to read as the secondary value.
    s_label_time_remain = lv_label_create(s_header_container);
    lv_label_set_text(s_label_time_remain, "-00:00.00");
    lv_obj_set_style_text_font(s_label_time_remain, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_time_remain, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_update_layout(s_label_time);  // ensure elapsed size is known before aligning
    lv_obj_align_to(s_label_time_remain, s_label_time, LV_ALIGN_OUT_RIGHT_MID, 14, 0);

    // BPM & Pitch Info (pulled to the far right edge of the header)
    lv_obj_t *bpm_info_container = lv_obj_create(s_header_container);
    lv_obj_remove_style_all(bpm_info_container);
    lv_obj_set_size(bpm_info_container, 130, 45);
    lv_obj_align(bpm_info_container, LV_ALIGN_RIGHT_MID, -8, 0);

    s_label_bpm = lv_label_create(bpm_info_container);
    lv_label_set_text(s_label_bpm, "120.00");
    lv_obj_set_style_text_font(s_label_bpm, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_bpm, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_pos(s_label_bpm, 10, 2);

    lv_obj_t *label_bpm_unit = lv_label_create(bpm_info_container);
    lv_label_set_text(label_bpm_unit, "BPM");
    lv_obj_set_style_text_font(label_bpm_unit, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpm_unit, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(label_bpm_unit, 75, 7);

    s_label_pitch = lv_label_create(bpm_info_container);
    lv_label_set_text(s_label_pitch, "+0.00%");
    lv_obj_set_style_text_font(s_label_pitch, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_pitch, COL_GREEN, LV_PART_MAIN);
    lv_obj_set_pos(s_label_pitch, 10, 23);

    lv_obj_t *label_pitch_unit = lv_label_create(bpm_info_container);
    lv_label_set_text(label_pitch_unit, "PITCH");
    lv_obj_set_style_text_font(label_pitch_unit, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_pitch_unit, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(label_pitch_unit, 75, 26);
}

// Build Pioneered-style top navigation buttons.
static void create_footer(lv_obj_t *parent) {
    s_footer_container = lv_obj_create(parent);
    lv_obj_remove_style_all(s_footer_container);
    lv_obj_add_style(s_footer_container, &s_style_footer, LV_PART_MAIN);
    lv_obj_set_size(s_footer_container, UI_HOR_RES, UI_TOPBAR_H);
    lv_obj_set_pos(s_footer_container, 0, 0);

    const int btn_width = 106;
    const int btn_height = 28;
    const int spacing = 6;
    const int offset_left = 6;
    const int offset_top = 9;

    for (int i = 0; i < 7; i++) {
        s_footer_buttons[i] = lv_button_create(s_footer_container);
        lv_obj_remove_style_all(s_footer_buttons[i]);
        lv_obj_add_style(s_footer_buttons[i], &s_style_tab_btn_normal, LV_PART_MAIN);
        lv_obj_add_style(s_footer_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(s_footer_buttons[i], btn_width, btn_height);
        lv_obj_set_pos(s_footer_buttons[i], offset_left + i * (btn_width + spacing), offset_top);
        lv_obj_clear_flag(s_footer_buttons[i], LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_user_data(s_footer_buttons[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(s_footer_buttons[i], footer_btn_event_cb, LV_EVENT_CLICKED, NULL);

        s_footer_active_strips[i] = lv_obj_create(s_footer_buttons[i]);
        lv_obj_remove_style_all(s_footer_active_strips[i]);
        lv_obj_set_size(s_footer_active_strips[i], btn_width - 10, 2);
        lv_obj_set_pos(s_footer_active_strips[i], 5, btn_height - 4);
        lv_obj_set_style_bg_color(s_footer_active_strips[i], COL_TAB_ACTIVE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_footer_active_strips[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_footer_active_strips[i], 0, LV_PART_MAIN);
        lv_obj_add_flag(s_footer_active_strips[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *lbl = lv_label_create(s_footer_buttons[i]);
        lv_label_set_text(lbl, s_tab_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }

    // Set first tab as active
    lv_obj_add_style(s_footer_buttons[0], &s_style_tab_btn_active, LV_PART_MAIN);
    lv_obj_remove_flag(s_footer_active_strips[0], LV_OBJ_FLAG_HIDDEN);
}

// Screen 3: HOT CUES Layout (2x4 Grid of pads)
static void create_screen_hot_cues(lv_obj_t *parent) {
    s_screens[2] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[2]);
    lv_obj_add_style(s_screens[2], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[2], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[2], 0, UI_CONTENT_Y);
    ui_controls_create_performance_target_selector(s_screens[2], 298, 4);

    // Construct a grid of buttons: 4 in a row, 2 rows
    int pad_w = 170;
    int pad_h = 130;
    int spacing_x = 20;
    int spacing_y = 20;
    int offset_x = 30;
    int offset_y = 48;

    for (int i = 0; i < 8; i++) {
        int row = i / 4;
        int col = i % 4;

        s_hot_cue_buttons[i] = lv_button_create(s_screens[2]);
        lv_obj_remove_style_all(s_hot_cue_buttons[i]);
        lv_obj_add_style(s_hot_cue_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        ui_style_hot_cue_pad(i, false, false);
        lv_obj_set_size(s_hot_cue_buttons[i], pad_w, pad_h);
        lv_obj_set_pos(s_hot_cue_buttons[i], offset_x + col * (pad_w + spacing_x), offset_y + row * (pad_h + spacing_y));

        lv_obj_set_user_data(s_hot_cue_buttons[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(s_hot_cue_buttons[i], hot_cue_event_cb, LV_EVENT_CLICKED, NULL);

        // Pad Title label
        lv_obj_t *lbl_pad = lv_label_create(s_hot_cue_buttons[i]);
        lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
        lv_obj_set_style_text_font(lbl_pad, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_pad, COL_GREEN, LV_PART_MAIN);
        lv_obj_align(lbl_pad, LV_ALIGN_TOP_LEFT, 10, 10);

        // Pad Time label
        lv_obj_t *lbl_time = lv_label_create(s_hot_cue_buttons[i]);
        char time_buf[16];
        ui_controls_hot_cue_t cue = ui_controls_hot_cue(&s_controls, (uint8_t)i);
        ui_format_time_cc(time_buf, sizeof(time_buf), cue.position_ms);
        lv_label_set_text(lbl_time, time_buf);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_time, COL_TEXT, LV_PART_MAIN);
        lv_obj_align(lbl_time, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }

    lv_obj_t *status_strip = lv_obj_create(s_screens[2]);
    lv_obj_remove_style_all(status_strip);
    lv_obj_add_style(status_strip, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(status_strip, 740, 62);
    lv_obj_set_pos(status_strip, 30, 360);
    lv_obj_clear_flag(status_strip, LV_OBJ_FLAG_SCROLLABLE);
    ui_settings_value_label(status_strip, "HOT CUE STATUS", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 12);
    ui_static_tile(status_strip, 176, 12, 90, 36, "CUE A-H", COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_static_tile(status_strip, 278, 12, 104, 36, "LOOP CUES", COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_static_tile(status_strip, 394, 12, 112, 36, "ANLZ DATA", COL_ACCENT, COL_PANEL_DK, COL_ACCENT);
    ui_static_tile(status_strip, 518, 12, 142, 36, "D1/D2 TARGET", COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
}

// Screen 4: BEAT LOOP Layout
static void create_screen_beat_loop(lv_obj_t *parent) {
    s_screens[3] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[3]);
    lv_obj_add_style(s_screens[3], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[3], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[3], 0, UI_CONTENT_Y);
    ui_controls_create_performance_target_selector(s_screens[3], 20, 8);

    s_label_loop_status = lv_label_create(s_screens[3]);
    ui_label_set_small_caps(s_label_loop_status, "NO ACTIVE LOOP", COL_TEXT_DIM);
    lv_obj_align(s_label_loop_status, LV_ALIGN_TOP_MID, 0, 12);

    // Predefined loops: 1/2, 1, 2, 4, 8, 16 beats
    int loop_beats[6] = {1, 2, 4, 8, 16, 32}; // 1 = 1 beat, 32 = 32 beats
    const char *loop_labels[6] = {"1 BEAT", "2 BEATS", "4 BEATS", "8 BEATS", "16 BEATS", "32 BEATS"};

    int pad_w = 210;
    int pad_h = 100;
    int spacing_x = 30;
    int spacing_y = 20;
    int offset_x = 60;
    int offset_y = 54;

    for (int i = 0; i < 6; i++) {
        int row = i / 3;
        int col = i % 3;

        s_loop_buttons[i] = lv_button_create(s_screens[3]);
        lv_obj_remove_style_all(s_loop_buttons[i]);
        lv_obj_add_style(s_loop_buttons[i], &s_style_btn_secondary, LV_PART_MAIN);
        lv_obj_add_style(s_loop_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(s_loop_buttons[i], pad_w, pad_h);
        lv_obj_set_pos(s_loop_buttons[i], offset_x + col * (pad_w + spacing_x), offset_y + row * (pad_h + spacing_y));

        lv_obj_set_user_data(s_loop_buttons[i], (void*)(intptr_t)loop_beats[i]);
        lv_obj_add_event_cb(s_loop_buttons[i], loop_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_loop = lv_label_create(s_loop_buttons[i]);
        lv_label_set_text(lbl_loop, loop_labels[i]);
        lv_obj_set_style_text_font(lbl_loop, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_loop, COL_TEXT, LV_PART_MAIN);
        lv_obj_align(lbl_loop, LV_ALIGN_CENTER, 0, 0);
    }

    // EXIT LOOP Button
    lv_obj_t *btn_exit = lv_button_create(s_screens[3]);
    lv_obj_remove_style_all(btn_exit);
    lv_obj_set_style_bg_color(btn_exit, COL_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_exit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_exit, lv_color_hex(0xFF6B85), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_exit, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_exit, 6, LV_PART_MAIN);
    lv_obj_add_style(btn_exit, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_exit, 180, 50);
    lv_obj_set_pos(btn_exit, 310, 290);
    lv_obj_add_event_cb(btn_exit, exit_loop_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "EXIT LOOP");
    lv_obj_set_style_text_font(lbl_exit, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_exit, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_exit, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *loop_strip = lv_obj_create(s_screens[3]);
    lv_obj_remove_style_all(loop_strip);
    lv_obj_add_style(loop_strip, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(loop_strip, 740, 62);
    lv_obj_set_pos(loop_strip, 30, 360);
    lv_obj_clear_flag(loop_strip, LV_OBJ_FLAG_SCROLLABLE);
    ui_settings_value_label(loop_strip, "LOOP TOOLS", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 12);
    ui_static_tile(loop_strip, 176, 12, 96, 36, "IN", COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(loop_strip, 284, 12, 96, 36, "OUT", COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(loop_strip, 392, 12, 118, 36, "RELOOP", COL_DISABLED, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(loop_strip, 522, 12, 124, 36, "ACTIVE SIZE", COL_GREEN, COL_PANEL_DK, COL_GREEN);

    ui_update_loop_screen_state();
}

// Screen 5: BEAT JUMP Layout
static void create_screen_beat_jump(lv_obj_t *parent) {
    s_screens[4] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[4]);
    lv_obj_add_style(s_screens[4], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[4], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[4], 0, UI_CONTENT_Y);
    ui_controls_create_performance_target_selector(s_screens[4], 298, 0);

    int jump_vals[4] = {1, 4, 8, 16};
    const int lane_x = 40;
    const int lane_w = 720;
    const int lane_h = 132;
    const int lane_gap = 24;
    const int lane_top_y = 34;
    const int btn_w = 150;
    const int spacing_x = 24;
    const int btn_x0 = 36;
    const int btn_y = 38;

    lv_obj_t *lane_back = lv_obj_create(s_screens[4]);
    lv_obj_remove_style_all(lane_back);
    lv_obj_add_style(lane_back, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(lane_back, lane_w, lane_h);
    lv_obj_set_pos(lane_back, lane_x, lane_top_y);
    lv_obj_clear_flag(lane_back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_back = lv_label_create(lane_back);
    ui_label_set_small_caps(lbl_back, "BACKWARD", COL_AMBER);
    lv_obj_set_pos(lbl_back, 16, 12);

    for (int i = 0; i < 4; i++) {
        ui_create_beat_jump_button(lane_back, btn_x0 + i * (btn_w + spacing_x), btn_y,
                                   -jump_vals[i], false);
    }

    lv_obj_t *lane_forward = lv_obj_create(s_screens[4]);
    lv_obj_remove_style_all(lane_forward);
    lv_obj_add_style(lane_forward, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(lane_forward, lane_w, lane_h);
    lv_obj_set_pos(lane_forward, lane_x, lane_top_y + lane_h + lane_gap);
    lv_obj_clear_flag(lane_forward, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_forward = lv_label_create(lane_forward);
    ui_label_set_small_caps(lbl_forward, "FORWARD", COL_GREEN);
    lv_obj_set_pos(lbl_forward, 16, 12);

    for (int i = 0; i < 4; i++) {
        ui_create_beat_jump_button(lane_forward, btn_x0 + i * (btn_w + spacing_x), btn_y,
                                   jump_vals[i], true);
    }

    lv_obj_t *jump_strip = lv_obj_create(s_screens[4]);
    lv_obj_remove_style_all(jump_strip);
    lv_obj_add_style(jump_strip, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(jump_strip, 720, 62);
    lv_obj_set_pos(jump_strip, 40, 346);
    lv_obj_clear_flag(jump_strip, LV_OBJ_FLAG_SCROLLABLE);
    ui_settings_value_label(jump_strip, "GRID / QUANTIZE", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 12);
    ui_static_tile(jump_strip, 184, 12, 106, 36, "BEAT GRID", COL_ACCENT, COL_PANEL_DK, COL_ACCENT);
    ui_static_tile(jump_strip, 302, 12, 110, 36, "QUANTIZE", COL_DISABLED, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(jump_strip, 424, 12, 116, 36, "SNAP: ON", COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_static_tile(jump_strip, 552, 12, 112, 36, "D1/D2", COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
}

// Screen 6: KEY SHIFT / KEYLOCK Layout
static void create_screen_key_shift(lv_obj_t *parent) {
    s_screens[5] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[5]);
    lv_obj_add_style(s_screens[5], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[5], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[5], 0, UI_CONTENT_Y);
    ui_controls_create_performance_target_selector(s_screens[5], 298, 4);

    const int panel_y = 54;
    const int panel_h = 282;

    lv_obj_t *tempo_panel = lv_obj_create(s_screens[5]);
    lv_obj_remove_style_all(tempo_panel);
    lv_obj_add_style(tempo_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(tempo_panel, 320, panel_h);
    lv_obj_set_pos(tempo_panel, 50, panel_y);
    lv_obj_clear_flag(tempo_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_tempo = lv_label_create(tempo_panel);
    ui_label_set_small_caps(lbl_tempo, "MASTER TEMPO", COL_TEXT_MUTED);
    lv_obj_set_pos(lbl_tempo, 18, 16);

    lv_obj_t *lbl_keylock = lv_label_create(tempo_panel);
    lv_label_set_text(lbl_keylock, "KEY LOCK");
    lv_obj_set_style_text_font(lbl_keylock, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_keylock, COL_GREEN, LV_PART_MAIN);
    lv_obj_align(lbl_keylock, LV_ALIGN_TOP_LEFT, 18, 64);

    lv_obj_t *sw_keylock = lv_switch_create(tempo_panel);
    lv_obj_set_pos(sw_keylock, 18, 130);
    lv_obj_add_event_cb(sw_keylock, keylock_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_preserves = lv_label_create(tempo_panel);
    lv_label_set_text(lbl_preserves, "PRESERVES KEY");
    lv_obj_set_style_text_font(lbl_preserves, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_preserves, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(lbl_preserves, 18, 174);

    ui_settings_value_label(tempo_panel, "TEMPO RANGE", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 18, 208);
    ui_static_tile(tempo_panel, 18, 234, 58, 30, "6%", COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(tempo_panel, 84, 234, 58, 30, "10%", COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(tempo_panel, 150, 234, 58, 30, "16%", COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_static_tile(tempo_panel, 216, 234, 76, 30, "WIDE", COL_TEXT, COL_PANEL_DK, COL_BORDER);

    lv_obj_t *transpose_panel = lv_obj_create(s_screens[5]);
    lv_obj_remove_style_all(transpose_panel);
    lv_obj_add_style(transpose_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(transpose_panel, 360, panel_h);
    lv_obj_set_pos(transpose_panel, 410, panel_y);
    lv_obj_clear_flag(transpose_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_transpose = lv_label_create(transpose_panel);
    ui_label_set_small_caps(lbl_transpose, "KEY TRANSPOSE", COL_TEXT_MUTED);
    lv_obj_set_pos(lbl_transpose, 18, 16);

    lv_obj_t *lbl_key_value = lv_label_create(transpose_panel);
    lv_label_set_text(lbl_key_value, "ORIGINAL KEY");
    lv_obj_set_style_text_font(lbl_key_value, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_key_value, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_key_value, LV_ALIGN_TOP_LEFT, 18, 84);

    lv_obj_t *lbl_no_transpose = lv_label_create(transpose_panel);
    lv_label_set_text(lbl_no_transpose, "NO TRANSPOSITION");
    lv_obj_set_style_text_font(lbl_no_transpose, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_no_transpose, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(lbl_no_transpose, 18, 150);

    ui_settings_value_label(transpose_panel, "KEY CONTROL", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 18, 190);
    ui_static_tile(transpose_panel, 18, 218, 88, 42, "- KEY", COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_static_tile(transpose_panel, 118, 218, 104, 42, "RESET", COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
    ui_static_tile(transpose_panel, 234, 218, 88, 42, "+ KEY", COL_GREEN, COL_PANEL_DK, COL_GREEN);

    lv_obj_t *pitch_strip = lv_obj_create(s_screens[5]);
    lv_obj_remove_style_all(pitch_strip);
    lv_obj_add_style(pitch_strip, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(pitch_strip, 720, 66);
    lv_obj_set_pos(pitch_strip, 50, 352);
    lv_obj_clear_flag(pitch_strip, LV_OBJ_FLAG_SCROLLABLE);

    ui_settings_value_label(pitch_strip, "PITCH / SYNC PLACEHOLDERS", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 18, 14);
    ui_static_tile(pitch_strip, 250, 14, 88, 38, "- PITCH", COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_static_tile(pitch_strip, 350, 14, 88, 38, "RESET", COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
    ui_static_tile(pitch_strip, 450, 14, 88, 38, "+ PITCH", COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_static_tile(pitch_strip, 560, 14, 92, 38, "SYNC", COL_DISABLED, COL_PANEL_DK, COL_BORDER);
}

// Screen 7: SETTINGS Layout
static void create_screen_settings(lv_obj_t *parent) {
    s_screens[6] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[6]);
    lv_obj_add_style(s_screens[6], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[6], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[6], 0, UI_CONTENT_Y);

    // Saved settings (firmware); the simulator uses defaults.
#ifndef WIN32
    app_settings_t cfg = app_settings_get();
    int  bl_init  = cfg.backlight_pct;
    bool rca_init = (cfg.audio_out != 0);
#else
    int  bl_init  = 80;
    bool rca_init = false;
#endif

    const int left_x = 30;
    const int left_w = 350;

    lv_obj_t *display_section = ui_settings_section(s_screens[6], left_x, 20, left_w, 86, "DISPLAY");
    s_slider_backlight = lv_slider_create(display_section);
    lv_obj_set_size(s_slider_backlight, 230, 18);
    lv_obj_set_pos(s_slider_backlight, 16, 48);
    lv_slider_set_range(s_slider_backlight, 10, 100);
    lv_slider_set_value(s_slider_backlight, bl_init, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_slider_backlight, slider_brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_brightness_val = ui_settings_value_label(display_section, "", COL_TEXT,
                                                     &lv_font_montserrat_14, 270, 44);
    lv_label_set_text_fmt(s_label_brightness_val, "%d%%", bl_init);

    lv_obj_t *audio_section = ui_settings_section(s_screens[6], left_x, 118, left_w, 86, "AUDIO OUTPUT");
    lv_obj_t *sw_audio = lv_switch_create(audio_section);
    lv_obj_set_pos(sw_audio, 16, 42);
    lv_obj_add_event_cb(sw_audio, audio_out_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_audio_out = ui_settings_value_label(audio_section, rca_init ? "RCA LINE-OUT" : "SPEAKER",
                                                COL_GREEN, &lv_font_montserrat_16, 104, 44);
    if (rca_init) {
        lv_obj_add_state(sw_audio, LV_STATE_CHECKED);
    }

    lv_obj_t *link_section = ui_settings_section(s_screens[6], left_x, 216, left_w, 116, "CDJ LINK ROLE");
    lv_obj_t *btn_link = lv_button_create(link_section);
    lv_obj_remove_style_all(btn_link);
    lv_obj_add_style(btn_link, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_link, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_link, 210, 42);
    lv_obj_set_pos(btn_link, 16, 42);
#ifndef WIN32
    lv_obj_add_event_cb(btn_link, link_mode_event_cb, LV_EVENT_CLICKED, NULL);
#endif

    s_label_link_mode = lv_label_create(btn_link);
#ifndef WIN32
    lv_label_set_text_fmt(s_label_link_mode, "%s", ui_link_mode_name(cfg.link_mode));
#else
    lv_label_set_text(s_label_link_mode, "OFF");
#endif
    lv_obj_set_style_text_font(s_label_link_mode, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_link_mode, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_label_link_mode, LV_ALIGN_CENTER, 0, 0);

    ui_settings_value_label(link_section, "Saved role applies after reboot", COL_AMBER,
                            &lv_font_montserrat_12, 16, 90);

    lv_obj_t *status_section = ui_settings_section(s_screens[6], 410, 20, 360, 312, "SYSTEM STATUS");

    s_label_uart_status = ui_settings_value_label(status_section,
                                                  "Control Link (S3): Offline (no heartbeat)",
                                                  COL_RED, &lv_font_montserrat_12, 16, 46);
    lv_obj_set_width(s_label_uart_status, 320);
    lv_label_set_long_mode(s_label_uart_status, LV_LABEL_LONG_CLIP);

    s_label_link_status = ui_settings_value_label(status_section, "Link: OFF",
                                                  COL_ACCENT, &lv_font_montserrat_12, 16, 74);
    lv_obj_set_width(s_label_link_status, 320);
    lv_label_set_long_mode(s_label_link_status, LV_LABEL_LONG_CLIP);

    ui_settings_value_label(status_section, "SD Card", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 116);
    s_label_sd_status = ui_settings_value_label(status_section, "Checking /sd...",
                                                COL_TEXT_DIM, &lv_font_montserrat_12, 16, 140);
    lv_obj_set_width(s_label_sd_status, 320);
    lv_label_set_long_mode(s_label_sd_status, LV_LABEL_LONG_CLIP);

    ui_settings_value_label(status_section, "Remote Cache", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 176);
    s_label_sd_cache_status = ui_settings_value_label(status_section, "Checking cache...",
                                                      COL_TEXT_DIM, &lv_font_montserrat_12, 16, 200);
    lv_obj_set_width(s_label_sd_cache_status, 210);
    lv_label_set_long_mode(s_label_sd_cache_status, LV_LABEL_LONG_CLIP);

    lv_obj_t *btn_clear_cache = lv_button_create(status_section);
    lv_obj_remove_style_all(btn_clear_cache);
    lv_obj_add_style(btn_clear_cache, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_clear_cache, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_clear_cache, 116, 34);
    lv_obj_set_pos(btn_clear_cache, 226, 190);
#ifndef WIN32
    lv_obj_add_event_cb(btn_clear_cache, sd_cache_clear_event_cb, LV_EVENT_CLICKED, NULL);
#endif

    lv_obj_t *lbl_clear_cache = lv_label_create(btn_clear_cache);
    lv_label_set_text(lbl_clear_cache, "CLEAR");
    lv_obj_set_style_text_font(lbl_clear_cache, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_clear_cache, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_clear_cache, LV_ALIGN_CENTER, 0, 0);

    ui_settings_value_label(status_section, "Board: JC4880P443C_I_W (ESP32-P4 N16R8)",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 252);
    ui_settings_value_label(status_section, "Firmware: Main Deck Engine v1.0.0-Beta (IDF v5.5)",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 276);

    lv_obj_t *mixer_section = ui_settings_section(s_screens[6], 30, 346, 740, 70, "MIXER / PFL ROUTING");
    ui_static_tile(mixer_section, 18, 30, 108, 28, "CH1 FADER", COL_ACCENT, COL_PANEL_DK, COL_ACCENT);
    ui_static_tile(mixer_section, 138, 30, 108, 28, "CH2 FADER", COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_static_tile(mixer_section, 258, 30, 118, 28, "CROSSFADER", COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
    ui_static_tile(mixer_section, 388, 30, 88, 28, "PFL D1", COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_static_tile(mixer_section, 488, 30, 88, 28, "PFL D2", COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_static_tile(mixer_section, 588, 30, 124, 28, "CUE PATH TODO", COL_DISABLED, COL_PANEL_DK, COL_BORDER);

#ifndef WIN32
    ui_update_link_status_label();
    ui_update_sd_status_label(true);
    ui_update_sd_cache_status_label(true);
#endif
}

// Overview waveform bridge. Library/track-load code still owns cache invalidation
// and metadata selection; the overview module owns all widgets and rendering.
static void ui_load_waveform_data(uint8_t deck,
                                  uint32_t duration_ms,
                                  const uint8_t waveform_low[400],
                                  bool has_waveform,
                                  const anlz_metadata_t *meta)
{
    ui_cache_invalidate();
    ui_overview_load_waveform_data(deck, duration_ms, waveform_low, has_waveform, meta);
}

static void ui_library_set_header_track(const char *title, const char *artist, uint16_t bpm)
{
    if (s_label_title) {
        lv_label_set_text(s_label_title, title && title[0] ? title : "Unknown Title");
    }
    if (s_label_artist) {
        lv_label_set_text(s_label_artist, artist && artist[0] ? artist : "Unknown Artist");
    }
    if (s_label_bpm) {
        ui_label_set_f2(s_label_bpm, (float)bpm);
    }
}

static bool ui_library_is_performance_target_active(uint8_t deck)
{
    return ui_controls_is_active_deck(&s_controls, deck);
}

static void ui_update_overview_cue_markers(uint8_t deck)
{
    ui_overview_update_cue_markers(deck, ui_deck_anlz(deck), ui_deck_duration_ms(deck));
}

/* Update Hot Cue pads with real Rekordbox cue metadata */
static void ui_update_hot_cues(void)
{
    uint8_t deck = ui_controls_active_deck(&s_controls);
    const anlz_metadata_t *meta = ui_performance_anlz();
    bool has_anlz = (meta != NULL);

    for (int i = 0; i < 8; i++) {
        bool found = false;
        uint32_t pos = 0;
        uint32_t end_pos = 0;
        uint8_t type = 1; // 1 = ANLZ_CUE_SINGLE, 2 = ANLZ_CUE_LOOP
        
        if (has_anlz) {
            for (int j = 0; j < meta->cue_count; j++) {
                if (meta->cues[j].index == i) {
                    pos = meta->cues[j].start_ms;
                    end_pos = meta->cues[j].end_ms;
                    type = (uint8_t)meta->cues[j].type;
                    found = true;
                    break;
                }
            }
        }
        
        if (found) {
            ui_controls_set_hot_cue(&s_controls, (uint8_t)i, pos, end_pos, type, false);
            
            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                char time_buf[16];
                ui_format_time_cc(time_buf, sizeof(time_buf), pos);
                lv_label_set_text(lbl_time, time_buf);
            }
            
            lv_obj_t *lbl_pad = lv_obj_get_child(s_hot_cue_buttons[i], 0);
            
            bool is_loop = (type == 2);
            if (is_loop) {
                if (lbl_pad) {
                    lv_label_set_text_fmt(lbl_pad, "LOOP %c", 'A' + i);
                }
            } else {
                if (lbl_pad) {
                    lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
                }
            }
            ui_style_hot_cue_pad(i, is_loop, false);
            
        } else if (has_anlz) {
            ui_controls_set_hot_cue(&s_controls,
                                    (uint8_t)i,
                                    0,
                                    0,
                                    UI_CONTROLS_HOT_CUE_SINGLE,
                                    true);
            
            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                lv_label_set_text(lbl_time, "EMPTY");
            }
            lv_obj_t *lbl_pad = lv_obj_get_child(s_hot_cue_buttons[i], 0);
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
            }
            ui_style_hot_cue_pad(i, false, true);
            
        } else {
            /* Simulator / Fallback default values */
            uint32_t default_pos = i * 15000;
            if (i >= 5) default_pos = (i - 1) * 30000;
            
            ui_controls_set_hot_cue(&s_controls,
                                    (uint8_t)i,
                                    default_pos,
                                    0,
                                    UI_CONTROLS_HOT_CUE_SINGLE,
                                    false);
            
            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                char time_buf[16];
                ui_format_time_cc(time_buf, sizeof(time_buf), default_pos);
                lv_label_set_text(lbl_time, time_buf);
            }
            lv_obj_t *lbl_pad = lv_obj_get_child(s_hot_cue_buttons[i], 0);
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
            }
            ui_style_hot_cue_pad(i, false, false);
            
        }
    }

    ui_update_overview_cue_markers(deck);
}

// ─── Global Interface Functions ──────────────────────────────────────────────

static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    ui_update();
}

#ifndef WIN32
static void ui_perf_log_us(const char *label, const ui_overview_perf_report_t *report)
{
    if (!label || !report) {
        return;
    }

    ESP_LOGI(TAG, "%s: last=%u us avg=%u us max=%u us samples=%u",
             label,
             (unsigned)report->last_us,
             (unsigned)report->avg_us,
             (unsigned)report->max_us,
             (unsigned)report->samples);
}

#endif

esp_err_t ui_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL DJ UI layout (800x480 landscape)...");
    ui_deck_anlz_store_init(&s_deck_anlz_store);
    ui_controls_state_init(&s_controls);

#ifndef WIN32
    // On firmware, bring up LVGL on top of the BSP panel before building widgets.
    // (On the PC simulator the HAL has already initialised LVGL + a display.)
    esp_err_t be_rc = ui_lvgl_backend_init(UI_HOR_RES, UI_VER_RES);
    if (be_rc != ESP_OK) {
        return be_rc;
    }
#endif

    // Initialize custom dark themes
    init_styles();

    ui_controls_widget_config_t controls_widget_config = {
        .pressed = &s_style_pressed,
        .select_deck = ui_set_performance_deck,
        .set_overview_target = ui_overview_set_performance_target,
    };
    ui_controls_widgets_init(&controls_widget_config);

    ui_overview_config_t overview_config = {
        .styles = {
            .screen_bg = &s_style_screen_bg,
            .panel_frame = &s_style_panel_frame,
            .btn_primary = &s_style_btn_primary,
            .btn_amber = &s_style_btn_amber,
            .pressed = &s_style_pressed,
        },
        .actions = {
            .select_deck = ui_set_performance_deck,
            .play_pause = ui_overview_action_play_pause,
            .cue = ui_overview_action_cue,
            .seek = ui_overview_action_seek,
        },
    };
    ui_overview_init(&overview_config);

    ui_library_config_t library_config = {
        .styles = {
            .screen_bg = &s_style_screen_bg,
            .btn_primary = &s_style_btn_primary,
            .btn_secondary = &s_style_btn_secondary,
            .btn_disabled = &s_style_btn_disabled,
            .pressed = &s_style_pressed,
        },
        .actions = {
            .status_hold = ui_status_indicator_hold,
            .status_color_for_text = ui_status_color_for_text,
            .cache_invalidate = ui_cache_invalidate,
            .set_header_track = ui_library_set_header_track,
            .clear_deck_track_info = ui_deck_track_info_clear,
            .set_deck_track_info = ui_deck_track_info_set,
            .set_deck_anlz = ui_deck_anlz_set_from_current,
            .get_deck_anlz = ui_deck_anlz,
            .load_waveform_data = ui_load_waveform_data,
            .set_loop_shadow = ui_set_loop_shadow,
            .is_performance_target_active = ui_library_is_performance_target_active,
            .update_hot_cues = ui_update_hot_cues,
            .update_loop_screen_state = ui_update_loop_screen_state,
        },
        .hor_res = UI_HOR_RES,
        .content_y = UI_CONTENT_Y,
        .content_h = UI_CONTENT_H,
    };
    ui_library_init(&library_config);

    // Create central base root container
    s_root_container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_root_container);
    lv_obj_add_style(s_root_container, &s_style_root, LV_PART_MAIN);
    lv_obj_set_size(s_root_container, 800, 480);

    // Initialize mock database system (if simulator)
#ifdef WIN32
    library_init();
    QueueHandle_t dummy;
    deck_core_init(&dummy);
#else
    ESP_ERROR_CHECK(media_catalog_init());
    if (app_settings_get().link_mode == WIFI_LINK_MODE_JOIN) {
        cdj_link_client_start();
    }
#endif

    // Build parts
    create_header(s_root_container);
    create_footer(s_root_container);

    // Build the 7 screen layers
    s_screens[0] = ui_overview_create(s_root_container);
    ui_controls_update_performance_target_visuals(&s_controls);
    s_screens[1] = ui_library_create(s_root_container);
    create_screen_hot_cues(s_root_container);
    create_screen_beat_loop(s_root_container);
    create_screen_beat_jump(s_root_container);
    create_screen_key_shift(s_root_container);
    create_screen_settings(s_root_container);

    // Switch initially to overview (index 0) and hide others
    for (int i = 1; i < 7; i++) {
        lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_active_tab = 0;

    ui_library_load_initial_track();

    // Register self-running LVGL timer to periodically refresh the UI states
    lv_timer_create(ui_timer_cb, 30, NULL);

#ifndef WIN32
    // Start the LVGL handler task last, once all widgets exist.
    esp_err_t start_rc = ui_lvgl_backend_start();
    if (start_rc != ESP_OK) {
        return start_rc;
    }
#endif

    ESP_LOGI(TAG, "LVGL DJ UI layout successfully initialized.");
    return ESP_OK;
}

static uint64_t ui_monotonic_time_us(void)
{
#ifndef WIN32
    return (uint64_t)esp_timer_get_time();
#else
    return (uint64_t)lv_tick_get() * 1000u;
#endif
}

static uint32_t ui_pitch_speed_permille(const deck_state_t *state)
{
    if (!state) {
        return 1000u;
    }

    float pitch_pct;
#ifndef WIN32
    pitch_pct = audio_engine_raw_pitch_to_percent(state->pitch);
#else
    pitch_pct = ((8192.0f - (float)state->pitch) / 8192.0f) * 10.0f;
#endif
    int speed = 1000 + (int)(pitch_pct * 10.0f + (pitch_pct >= 0.0f ? 0.5f : -0.5f));
    if (speed < 1) {
        speed = 1;
    }
    return (uint32_t)speed;
}

static void ui_build_frame_context(ui_frame_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->now_us = ui_monotonic_time_us();
    ctx->now_ms = lv_tick_get();
    ctx->active_tab = s_active_tab;

    ctx->deck_state[CTRL_DECK_1] = deck_core_get_state();
    ctx->deck_state[CTRL_DECK_2] = deck_core_get_deck_state(CTRL_DECK_2);
    ctx->active_deck = ui_controls_active_deck(&s_controls);
    ctx->active_state = ctx->deck_state[ui_deck_index(ctx->active_deck)];

    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        ctx->deck_duration_ms[deck] = ui_deck_duration_ms(deck);
        ctx->deck_bpm[deck] = ui_deck_bpm(deck);
        ctx->deck_meta[deck] = ui_deck_anlz(deck);
        ctx->deck_info[deck] = &s_deck_track_info[deck];
        ctx->deck_speed_permille[deck] =
            ui_pitch_speed_permille(&ctx->deck_state[deck]);

        const uint8_t *loaded_waveform_low = NULL;
        bool loaded_has_waveform = false;
        if (ui_library_get_loaded_waveform(deck, &loaded_waveform_low, &loaded_has_waveform)) {
            ctx->overview_wave_source[deck] = (ui_overview_waveform_source_info_t){
                .kind = UI_OVERVIEW_WAVEFORM_SOURCE_LOADED_MEDIA,
                .waveform_low = loaded_waveform_low,
                .has_waveform = loaded_has_waveform,
            };
        } else
        {
            ctx->overview_wave_source[deck] = (ui_overview_waveform_source_info_t){
                .kind = UI_OVERVIEW_WAVEFORM_SOURCE_METADATA,
                .waveform_low = NULL,
                .has_waveform = false,
            };
        }
    }

    ctx->active_duration_ms = ctx->deck_duration_ms[ui_deck_index(ctx->active_deck)];
    ctx->active_base_bpm = ctx->deck_bpm[ui_deck_index(ctx->active_deck)];
    ctx->active_meta = ctx->deck_meta[ui_deck_index(ctx->active_deck)];
    if (ctx->active_duration_ms > 0) {
        ctx->active_beat_state =
            ui_beat_indicator_calculate(ctx->active_state.position_ms,
                                        ctx->active_meta ? ctx->active_meta->beats : NULL,
                                        ctx->active_meta ? ctx->active_meta->beat_count : 0,
                                        ctx->active_base_bpm);
        ctx->active_beat_state_valid = ctx->active_beat_state.valid;
    }

    static uint32_t s_overview_slow_bucket = UINT32_MAX;
    uint32_t overview_slow_bucket = ctx->now_ms / 1000u;
    ctx->overview_slow_update = overview_slow_bucket != s_overview_slow_bucket;
    if (ctx->overview_slow_update) {
        s_overview_slow_bucket = overview_slow_bucket;
    }

#ifndef WIN32
    ctx->ae_loading = (audio_engine_get_state() == AE_LOADING);
    ctx->ae_load_pct = audio_engine_load_progress();
    audio_engine_get_mixer_snapshot(&ctx->mixer_snapshot);
#else
    ctx->ae_loading = false;
    ctx->ae_load_pct = 100;
#endif
}

void ui_update(void) {
#ifndef WIN32
    uint64_t update_start_us = 0;
    if (ui_diagnostics_enabled()) {
        update_start_us = (uint64_t)esp_timer_get_time();
        static uint64_t last_update_start_us = 0;
        if (last_update_start_us != 0) {
            ui_overview_perf_report_t interval_report;
            if (ui_overview_perf_record(&s_ui_update_interval_perf,
                                        (uint32_t)(update_start_us - last_update_start_us),
                                        &interval_report)) {
                ui_perf_log_us("ui_update interval", &interval_report);
            }
        }
        last_update_start_us = update_start_us;
    }
#endif

    ui_frame_context_t ctx;
    ui_build_frame_context(&ctx);
    ui_library_update(&ctx);

    deck_state_t state = ctx.deck_state[CTRL_DECK_1];
    uint8_t active_deck = ctx.active_deck;
    deck_state_t active_state = ctx.active_state;
    uint32_t active_duration_ms = ctx.active_duration_ms;
    uint16_t active_base_bpm = ctx.active_base_bpm;
    ui_update_active_header_track(active_deck);
    ui_beat_indicator_state_t beat_state = ctx.active_beat_state;
    bool beat_state_valid = ctx.active_beat_state_valid;
    bool ae_loading = ctx.ae_loading;
    uint8_t ae_load_pct = ctx.ae_load_pct;

    // ─── 1. Simulate Loop constraints inside update loop (for simulator) ───
#ifdef WIN32
    ui_controls_loop_state_t active_loop = ui_controls_active_loop(&s_controls);
    if (active_loop.active) {
        if (state.position_ms >= active_loop.end_ms) {
            mock_deck_set_position(active_loop.start_ms);
            state.position_ms = active_loop.start_ms;
        }
    }
#endif

    // ─── 2. Update Play / Pause state label ───
    if (ae_loading) {
        s_status_override_until_ms = 0;
        char loading_status[16];
        snprintf(loading_status, sizeof(loading_status), "D%u LOAD %u%%",
                 (unsigned)active_deck + 1u, (unsigned)ae_load_pct);
        ui_status_indicator_set(loading_status, COL_ACCENT);
    } else if (!ui_status_indicator_has_override()) {
        char status_text[16];
        snprintf(status_text, sizeof(status_text), "D%u %s",
                 (unsigned)active_deck + 1u,
                 active_state.playing ? "PLAY" : "PAUSE");
        if (active_state.playing) {
            ui_status_indicator_set(status_text, COL_GREEN);
        } else {
            ui_status_indicator_set(status_text, COL_AMBER);
        }
    }

    // ─── 3. Update BPM and Pitch % labels ───
    float pitch_pct;
#ifndef WIN32
    pitch_pct = audio_engine_raw_pitch_to_percent(active_state.pitch);
#else
    pitch_pct = ((8192.0f - (float)active_state.pitch) / 8192.0f) * 10.0f;
#endif
    // No %f in LVGL builtin printf — format the signed 2-decimal percent by hand.
    int pc = (int)(pitch_pct * 100.0f + (pitch_pct >= 0.0f ? 0.5f : -0.5f));
    if (pc != s_cache_pitch_centipct) {
        s_cache_pitch_centipct = pc;
        lv_label_set_text_fmt(s_label_pitch, "%c%d.%02d%%",
                              (pc < 0) ? '-' : '+', (pc < 0 ? -pc : pc) / 100, (pc < 0 ? -pc : pc) % 100);
    }

    float current_bpm = (float)(active_base_bpm ? active_base_bpm : 120) * (1.0f + (pitch_pct / 100.0f));
    int bpm_centi = (int)(current_bpm * 100.0f + (current_bpm >= 0.0f ? 0.5f : -0.5f));
    if (bpm_centi != s_cache_bpm_centi) {
        s_cache_bpm_centi = bpm_centi;
        ui_label_set_f2(s_label_bpm, current_bpm);
    }

    // ─── 4. Update time counters: elapsed (current position) + remaining ───
    uint32_t elapsed_ms  = active_state.position_ms;
    uint32_t remain_ms   = (active_duration_ms > elapsed_ms) ? (active_duration_ms - elapsed_ms) : 0;

    // Remaining-time warning colours: amber inside 30 s, red inside 10 s of the
    // end (only while a real track is loaded — idle/no-duration stays neutral).
    lv_color_t remain_col = COL_TEXT_MUTED;
    if (active_duration_ms > 0) {
        if (remain_ms <= 10000)      remain_col = lv_color_hex(0xFF1744); // red  <=10s
        else if (remain_ms <= 30000) remain_col = lv_color_hex(0xFFAB00); // amber <=30s
    }
    ui_obj_set_text_color_cached(s_label_time_remain, &s_cache_remain_color, remain_col);

    if (ae_loading) {
        if (!s_cache_time_loading) {
            lv_label_set_text(s_label_time, "LOADING");
            lv_label_set_text(s_label_time_remain, "");
            s_cache_time_loading = true;
            s_cache_elapsed_centis = UINT32_MAX;
            s_cache_remain_centis = UINT32_MAX;
        }
    } else {
        if (s_cache_time_loading) {
            s_cache_time_loading = false;
            s_cache_elapsed_centis = UINT32_MAX;
            s_cache_remain_centis = UINT32_MAX;
        }
        uint32_t elapsed_centis = elapsed_ms / 1000u;
        uint32_t remain_centis = remain_ms / 1000u;
        if (elapsed_centis != s_cache_elapsed_centis) {
            s_cache_elapsed_centis = elapsed_centis;
            lv_label_set_text_fmt(s_label_time, "%02u:%02u.%02u",
                                (unsigned)(elapsed_ms / 60000),
                                (unsigned)((elapsed_ms % 60000) / 1000),
                                (unsigned)((elapsed_ms % 1000) / 10));
        }
        if (remain_centis != s_cache_remain_centis) {
            s_cache_remain_centis = remain_centis;
            lv_label_set_text_fmt(s_label_time_remain, "-%02u:%02u.%02u",
                                (unsigned)(remain_ms / 60000),
                                (unsigned)((remain_ms % 60000) / 1000),
                                (unsigned)((remain_ms % 1000) / 10));
        }
    }

    // ─── 4B. Update S3 Beat LED feedback ───
#ifndef WIN32
    static uint8_t s_last_led_state[LED_COUNT] = {0xFF, 0xFF, 0xFF, 0xFF};
    static uint8_t s_last_led_deck = CTRL_DECK_NONE;
    if (s_last_led_deck != active_deck) {
        memset(s_last_led_state, 0xFF, sizeof(s_last_led_state));
        s_last_led_deck = active_deck;
    }

    ui_active_deck_leds_t leds =
        ui_active_deck_leds_calculate(active_state.playing,
                                      active_state.position_ms,
                                      active_state.cue_point_ms,
                                      active_duration_ms,
                                      beat_state_valid,
                                      beat_state.progress_permille);
    const uint8_t next_leds[LED_COUNT] = {
        [LED_CUE] = leds.cue,
        [LED_PLAY] = leds.play,
        [LED_BEAT] = leds.beat,
        [LED_END] = leds.end,
    };
    for (int led = 0; led < LED_COUNT; led++) {
        if (next_leds[led] != s_last_led_state[led]) {
            s_last_led_state[led] = next_leds[led];
            control_link_send_led((led_id_t)led, next_leds[led]);
        }
    }
#endif

    // ─── 5. Update Overview deck panels (Only if overview screen is visible) ───
    ui_overview_update(&ctx);

    // ─── 6. Sync UI Status bar with mock settings ───
    if (s_active_tab == 6) {
#ifndef WIN32
        ui_update_uart_status_label(&state);
        ui_update_link_status_label();
        ui_update_sd_status_label(false);
        ui_update_sd_cache_status_label(false);
#endif
    }

#ifndef WIN32
    if (ui_diagnostics_enabled()) {
        uint64_t update_end_us = (uint64_t)esp_timer_get_time();
        ui_overview_perf_report_t duration_report;
        if (ui_overview_perf_record(&s_ui_update_duration_perf,
                                    (uint32_t)(update_end_us - update_start_us),
                                    &duration_report)) {
            ui_perf_log_us("ui_update duration", &duration_report);
        }
    }
#endif
}
