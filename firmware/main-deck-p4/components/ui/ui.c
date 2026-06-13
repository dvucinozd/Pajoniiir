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
#include "ui_performance_tabs.h"
#include "ui_settings.h"
#include "ui_status.h"
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

// Sub-screen elements
static ui_deck_track_info_t s_deck_track_info[DECK_CORE_DECK_COUNT];
static ui_deck_anlz_store_t s_deck_anlz_store;
static ui_controls_state_t s_controls;

#ifndef WIN32
#endif

// UI update timing diagnostics
static ui_overview_perf_counter_t s_ui_update_interval_perf;
static ui_overview_perf_counter_t s_ui_update_duration_perf;

// Footer navigation buttons
static lv_obj_t *s_footer_buttons[7];
static lv_obj_t *s_footer_active_strips[7];
static const char *s_tab_names[7] = {
    "OVERVIEW", "LIBRARY", "HOT CUES", "LOOP", "BEAT JUMP", "KEY SHIFT", "SETTINGS"
};

// Settings Screen Widgets
static lv_obj_t *s_slider_backlight = NULL;
static lv_obj_t *s_label_brightness_val = NULL;
static lv_obj_t *s_label_audio_out = NULL;
static lv_obj_t *s_label_link_mode = NULL;

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

static void ui_set_performance_deck(uint8_t deck);
static void ui_load_waveform_data(uint8_t deck,
                                  uint32_t duration_ms,
                                  const uint8_t waveform_low[400],
                                  bool has_waveform,
                                  const anlz_metadata_t *meta);
static void ui_cache_invalidate(void)
{
    ui_status_invalidate();
    ui_settings_invalidate();
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

static uint32_t ui_performance_deck_position_ms(uint8_t deck)
{
#ifndef WIN32
    return audio_engine_deck_position_ms(deck);
#else
    return deck == CTRL_DECK_1 ? deck_core_get_state().position_ms
                               : deck_core_get_deck_state(deck).position_ms;
#endif
}

static void ui_performance_seek(uint8_t deck, uint32_t position_ms)
{
#ifndef WIN32
    audio_engine_deck_seek(deck, position_ms);
#else
    (void)deck;
    mock_deck_set_position(position_ms);
#endif
}

static void ui_performance_play(uint8_t deck)
{
#ifndef WIN32
    audio_engine_deck_play(deck);
#else
    (void)deck;
    mock_deck_set_playing(true);
#endif
}

static void ui_performance_set_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms)
{
#ifndef WIN32
    audio_engine_deck_set_loop(deck, start_ms, end_ms);
#else
    (void)deck;
    (void)start_ms;
    (void)end_ms;
#endif
}

static void ui_performance_clear_loop(uint8_t deck)
{
#ifndef WIN32
    audio_engine_deck_clear_loop(deck);
#else
    (void)deck;
#endif
}

static void ui_performance_toggle_master_tempo(void)
{
#ifdef WIN32
    mock_deck_toggle_master_tempo();
    deck_state_t state = deck_core_get_state();
    ESP_LOGI(TAG, "Master Tempo toggled: %s", state.master_tempo ? "ON" : "OFF");
#endif
}

static void ui_set_loop_shadow(uint8_t deck,
                               bool active,
                               uint32_t start_ms,
                               uint32_t end_ms,
                               int beats)
{
    ui_performance_tabs_set_loop_shadow(deck, active, start_ms, end_ms, beats);
}

static void ui_set_performance_deck(uint8_t deck)
{
    uint8_t before = ui_controls_active_deck(&s_controls);
    ui_controls_set_active_deck(&s_controls, ui_deck_index(deck));
    uint8_t after = ui_controls_active_deck(&s_controls);
    if (before != after) {
        ui_status_invalidate_header();
    }

    ui_controls_update_performance_target_visuals(&s_controls);
    ui_performance_tabs_update_loop_screen_state();
    ui_performance_tabs_update_hot_cues();

    if (before != after) {
        ui_status_hold(after == CTRL_DECK_1 ? "TARGET D1" : "TARGET D2",
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
    ui_settings_note_link_mode_saved(ui_link_mode_name(next));
    ESP_LOGI(TAG, "Link mode saved: %s", ui_link_mode_name(next));
}

static void sd_cache_clear_event_cb(lv_event_t *e)
{
    (void)e;
    if (ui_library_has_remote_loaded_track()) {
        ui_status_hold("REMOTE LOADED", COL_AMBER, 2000);
        sd_diag_log_write("sd_cache", "clear blocked while remote track is loaded");
        return;
    }

    esp_err_t rc = remote_cache_clear();
    if (rc == ESP_OK) {
        ui_status_hold("CACHE CLEARED", COL_GREEN, 2000);
        sd_diag_log_write("sd_cache", "remote cache cleared");
    } else {
        ui_status_hold("CACHE ERR", COL_RED, 2000);
        sd_diag_log_write("sd_cache", "remote cache clear failed");
    }
    ui_cache_invalidate();
    ui_settings_refresh_storage();
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

    ui_status_widgets_t status_widgets = {
        .title = s_label_title,
        .artist = s_label_artist,
        .time_elapsed = s_label_time,
        .time_remain = s_label_time_remain,
        .bpm = s_label_bpm,
        .pitch = s_label_pitch,
        .status_indicator = s_label_status_indicator,
    };
    ui_status_init(&status_widgets);
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

    lv_obj_t *label_uart_status =
        ui_settings_value_label(status_section,
                                "Control Link (S3): Offline (no heartbeat)",
                                COL_RED, &lv_font_montserrat_12, 16, 46);
    lv_obj_set_width(label_uart_status, 320);
    lv_label_set_long_mode(label_uart_status, LV_LABEL_LONG_CLIP);

    lv_obj_t *label_link_status =
        ui_settings_value_label(status_section, "Link: OFF",
                                COL_ACCENT, &lv_font_montserrat_12, 16, 74);
    lv_obj_set_width(label_link_status, 320);
    lv_label_set_long_mode(label_link_status, LV_LABEL_LONG_CLIP);

    ui_settings_value_label(status_section, "SD Card", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 116);
    lv_obj_t *label_sd_status =
        ui_settings_value_label(status_section, "Checking /sd...",
                                COL_TEXT_DIM, &lv_font_montserrat_12, 16, 140);
    lv_obj_set_width(label_sd_status, 320);
    lv_label_set_long_mode(label_sd_status, LV_LABEL_LONG_CLIP);

    ui_settings_value_label(status_section, "Remote Cache", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 176);
    lv_obj_t *label_sd_cache_status =
        ui_settings_value_label(status_section, "Checking cache...",
                                COL_TEXT_DIM, &lv_font_montserrat_12, 16, 200);
    lv_obj_set_width(label_sd_cache_status, 210);
    lv_label_set_long_mode(label_sd_cache_status, LV_LABEL_LONG_CLIP);

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

    ui_settings_widgets_t settings_widgets = {
        .uart_status = label_uart_status,
        .link_status = label_link_status,
        .sd_status = label_sd_status,
        .sd_cache_status = label_sd_cache_status,
    };
    ui_settings_init(&settings_widgets);
    ui_settings_refresh_storage();
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

static bool ui_library_is_performance_target_active(uint8_t deck)
{
    return ui_controls_is_active_deck(&s_controls, deck);
}

static void ui_update_overview_cue_markers(uint8_t deck)
{
    ui_overview_update_cue_markers(deck, ui_deck_anlz(deck), ui_deck_duration_ms(deck));
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

    ui_performance_tabs_config_t performance_tabs_config = {
        .controls = &s_controls,
        .styles = {
            .screen_bg = &s_style_screen_bg,
            .panel_frame = &s_style_panel_frame,
            .btn_secondary = &s_style_btn_secondary,
            .pressed = &s_style_pressed,
        },
        .actions = {
            .active_bpm = ui_performance_bpm,
            .active_anlz = ui_performance_anlz,
            .active_state = ui_performance_deck_state,
            .deck_position_ms = ui_performance_deck_position_ms,
            .seek = ui_performance_seek,
            .play = ui_performance_play,
            .set_loop = ui_performance_set_loop,
            .clear_loop = ui_performance_clear_loop,
            .toggle_master_tempo = ui_performance_toggle_master_tempo,
            .update_overview_cue_markers = ui_update_overview_cue_markers,
        },
        .hor_res = UI_HOR_RES,
        .content_y = UI_CONTENT_Y,
        .content_h = UI_CONTENT_H,
    };
    ui_performance_tabs_init(&performance_tabs_config);

    ui_library_config_t library_config = {
        .styles = {
            .screen_bg = &s_style_screen_bg,
            .btn_primary = &s_style_btn_primary,
            .btn_secondary = &s_style_btn_secondary,
            .btn_disabled = &s_style_btn_disabled,
            .pressed = &s_style_pressed,
        },
        .actions = {
            .status_hold = ui_status_hold,
            .status_color_for_text = ui_status_color_for_text,
            .cache_invalidate = ui_cache_invalidate,
            .set_header_track = ui_status_set_header_track,
            .clear_deck_track_info = ui_deck_track_info_clear,
            .set_deck_track_info = ui_deck_track_info_set,
            .set_deck_anlz = ui_deck_anlz_set_from_current,
            .get_deck_anlz = ui_deck_anlz,
            .load_waveform_data = ui_load_waveform_data,
            .set_loop_shadow = ui_set_loop_shadow,
            .is_performance_target_active = ui_library_is_performance_target_active,
            .update_hot_cues = ui_performance_tabs_update_hot_cues,
            .update_loop_screen_state = ui_performance_tabs_update_loop_screen_state,
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
    s_screens[2] = ui_performance_tabs_create_hot_cues(s_root_container);
    s_screens[3] = ui_performance_tabs_create_beat_loop(s_root_container);
    s_screens[4] = ui_performance_tabs_create_beat_jump(s_root_container);
    s_screens[5] = ui_performance_tabs_create_key_shift(s_root_container);
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

#ifdef WIN32
    deck_state_t state = ctx.deck_state[CTRL_DECK_1];
    ui_controls_loop_state_t active_loop = ui_controls_active_loop(&s_controls);
    if (active_loop.active) {
        if (state.position_ms >= active_loop.end_ms) {
            mock_deck_set_position(active_loop.start_ms);
            ctx.deck_state[CTRL_DECK_1].position_ms = active_loop.start_ms;
            if (ctx.active_deck == CTRL_DECK_1) {
                ctx.active_state.position_ms = active_loop.start_ms;
            }
        }
    }
#endif

    ui_status_update(&ctx);
    ui_overview_update(&ctx);
    ui_settings_update(&ctx);

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
