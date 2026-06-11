#include "ui.h"
#include "lvgl.h"
#include "ui_theme.h"   // centralised colour palette (COL_*); needs lvgl.h above
#include "esp_log.h"
#include "deck_core.h"
#include "library.h"
#include "ui_active_deck_leds.h"
#include "ui_beat_indicator.h"
#include "ui_deck_anlz_store.h"
#include "ui_mixer_view.h"
#include "ui_performance_target.h"
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
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <sys/lock.h>

#define LVGL_TICK_PERIOD_MS   2
#define LVGL_TASK_STACK       (24 * 1024)
#define LVGL_TASK_PRIO        2
#define UI_TRACK_LOAD_STACK   (10 * 1024)

// The UI canvas is 800x480 landscape; the physical ST7701 panel is 480x800
// portrait. LVGL renders landscape, then the flush callback uses the ESP32-P4
// PPA (Pixel Processing Accelerator) to rotate each full frame into the panel's
// MIPI-DSI frame buffer. LVGL's own software rotation is unusable here (partial
// mode corrupts the DSI DMA; full mode doesn't rotate), so we rotate in hardware.
#define UI_HOR_RES   800   // logical landscape width  (LVGL canvas)
#define UI_VER_RES   480   // logical landscape height
#define UI_TOPBAR_H   46
#define UI_CONTENT_Y  UI_TOPBAR_H
#define UI_CONTENT_H  (UI_VER_RES - UI_TOPBAR_H)
#define UI_DSI_FB_COUNT 3  // DPI framebuffers (num_fbs=3) for tear-free triple buffering
#define ALIGN_UP_BY(n, a)  (((n) + ((a) - 1)) & ~((a) - 1))

// LVGL is not thread-safe: any LVGL call outside the handler task must hold this.
static _lock_t              s_lvgl_lock;
static lv_display_t        *s_disp        = NULL;
static ppa_client_handle_t  s_ppa         = NULL;
// Triple buffering: the PPA rotates each frame into a NON-displayed framebuffer,
// then esp_lcd_panel_draw_bitmap() switches the DPI to it at the next frame
// boundary (tear-free). Rotating through 3 buffers gives 2 frames of slack so we
// never write the buffer currently on screen (LVGL flush rate ≤ panel refresh).
static void                *s_dsi_fb[UI_DSI_FB_COUNT] = { NULL };
static int                  s_dsi_fb_idx  = 1;   // next fb to render into (fb 0 active at boot)
static size_t               s_cache_align = 64;
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
static int       s_selected_track_idx = 0;
static lv_obj_t *s_library_table = NULL;
static volatile bool s_library_needs_refresh = false;
static bool      s_sort_artist_desc = false;
static bool      s_sort_name_desc = false;
static bool      s_sort_bpm_desc = false;
static bool      s_track_load_busy = false;
static uint8_t   s_library_load_request_deck = 0;

typedef struct {
    bool valid;
    char title[96];
    char artist[64];
    uint16_t bpm;
    uint32_t duration_ms;
} ui_deck_track_info_t;

static ui_deck_track_info_t s_deck_track_info[DECK_CORE_DECK_COUNT];
static ui_deck_anlz_store_t s_deck_anlz_store;
static ui_performance_target_t s_performance_target;

#ifndef WIN32
static media_loaded_track_t  s_loaded_media[DECK_CORE_DECK_COUNT];
static bool                  s_loaded_media_valid[DECK_CORE_DECK_COUNT];
static media_source_t        s_loaded_media_source[DECK_CORE_DECK_COUNT] = {
    MEDIA_SOURCE_LOCAL_USB,
    MEDIA_SOURCE_LOCAL_USB,
};
static QueueHandle_t         s_track_load_result_q = NULL;
static volatile bool         s_usb_removed_pending = false;
#endif

// Waveform visualizer definitions
#define OVERVIEW_CV_W 550
#define OVERVIEW_CV_H 78
#define OVERVIEW_MINI_CV_W 390
#define OVERVIEW_MINI_CV_H 30
#define OVERVIEW_WAVEFORM_LOW_SAMPLES 400
#define OVERVIEW_XFADER_X 314

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *label_deck;
    lv_obj_t *label_status;
    lv_obj_t *label_title;
    lv_obj_t *label_artist;
    lv_obj_t *label_time;
    lv_obj_t *label_remain;
    lv_obj_t *label_bpm;
    lv_obj_t *label_pitch;
    lv_obj_t *label_ch;
    lv_obj_t *label_out;
    lv_obj_t *label_pfl;
    lv_obj_t *out_bar_bg;
    lv_obj_t *out_bar_fill;
    lv_obj_t *wave_border;
    lv_obj_t *wave_canvas;
    uint8_t  *wave_buf;
    int       wave_stride_px;
    lv_obj_t *mini_wave_border;
    lv_obj_t *mini_wave_canvas;
    uint8_t  *mini_wave_buf;
    int       mini_wave_stride_px;
    lv_obj_t *mini_playhead;
    lv_obj_t *playhead;
    int       last_fill_x;
    int       last_mini_fill_x;
    int       last_playhead_x;
} ui_overview_deck_panel_t;

static ui_overview_deck_panel_t s_overview_decks[DECK_CORE_DECK_COUNT];
static lv_obj_t *s_beat_pulses[4];
static lv_obj_t *s_crossfader_label = NULL;
static lv_obj_t *s_crossfader_track = NULL;
static lv_obj_t *s_crossfader_knob = NULL;
static int       s_crossfader_track_w = 0;
static lv_obj_t *s_overview_fx_panel = NULL;

// Hot cue buttons and values
static uint32_t s_hot_cue_positions[8] = {
    0,      // Cue A: 0s
    15000,  // Cue B: 15s
    30000,  // Cue C: 30s
    45000,  // Cue D: 45s
    60000,  // Cue E: 60s
    90000,  // Cue F: 90s
    120000, // Cue G: 120s
    150000  // Cue H: 150s
};
static uint32_t s_hot_cue_ends[8] = {0};
static uint8_t  s_hot_cue_types[8] = {1, 1, 1, 1, 1, 1, 1, 1}; // 1 = ANLZ_CUE_SINGLE, 2 = ANLZ_CUE_LOOP
static lv_obj_t *s_hot_cue_buttons[8];
static lv_obj_t *s_overview_cue_markers[DECK_CORE_DECK_COUNT][8];
static lv_obj_t *s_loop_buttons[6];
static lv_obj_t *s_label_loop_status = NULL;
static int       s_loop_active_beats = 0;
static lv_obj_t *s_perf_target_buttons[10];
static size_t    s_perf_target_button_count = 0;

// Footer navigation buttons
static lv_obj_t *s_footer_buttons[7];
static lv_obj_t *s_footer_active_strips[7];
static const char *s_tab_names[7] = {
    "OVERVIEW", "LIBRARY", "HOT CUES", "LOOP", "BEAT JUMP", "KEY SHIFT", "SETTINGS"
};

// Loop Simulation settings
static bool     s_loop_active = false;
static uint32_t s_loop_start_ms = 0;
static uint32_t s_loop_end_ms = 0;
static bool     s_loop_active_by_deck[DECK_CORE_DECK_COUNT];
static uint32_t s_loop_start_ms_by_deck[DECK_CORE_DECK_COUNT];
static uint32_t s_loop_end_ms_by_deck[DECK_CORE_DECK_COUNT];
static int      s_loop_active_beats_by_deck[DECK_CORE_DECK_COUNT];

// Settings Screen Widgets
static lv_obj_t *s_slider_backlight = NULL;
static lv_obj_t *s_label_brightness_val = NULL;
static lv_obj_t *s_label_uart_status = NULL;
static lv_obj_t *s_label_audio_out = NULL;
static lv_obj_t *s_label_link_status = NULL;
static lv_obj_t *s_label_link_mode = NULL;
static lv_obj_t *s_label_sd_status = NULL;
static lv_obj_t *s_label_sd_cache_status = NULL;
static lv_obj_t *s_label_library_source = NULL;
static lv_obj_t *s_btn_library_load = NULL;
static lv_obj_t *s_btn_library_load_deck2 = NULL;
static lv_obj_t *s_label_library_hint = NULL;

typedef struct {
    bool valid;
    char text[80];
} ui_text_cache_t;

typedef struct {
    bool valid;
    uint32_t color;
} ui_color_cache_t;

typedef struct {
    bool valid;
    bool active;
    bool downbeat;
    lv_opa_t opa;
} ui_beat_dot_cache_t;

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
static ui_beat_dot_cache_t s_cache_beat_dots[4];
#ifndef WIN32
static audio_engine_mixer_snapshot_t s_cache_mixer_snapshot = {0};
#endif

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

#ifdef WIN32
static void ui_load_waveform(const library_track_t *track);
#endif
#ifndef WIN32
static void ui_load_waveform_media(uint8_t deck, const media_loaded_track_t *track);
#endif
static void ui_update_library_source_label(void);
#ifndef WIN32
static void ui_update_sd_status_label(bool force);
static void ui_update_sd_cache_status_label(bool force);
#endif
static void ui_update_hot_cues(void);
static void ui_refresh_loop_screen_from_target(void);
static void ui_update_performance_target_visuals(void);
static void ui_set_performance_deck(uint8_t deck);
static void ui_fill_library_row(int i);
static void jump_btn_event_cb(lv_event_t *e);
static void ui_load_waveform_data(uint8_t deck,
                                  uint32_t duration_ms,
                                  const uint8_t waveform_low[400],
                                  bool has_waveform,
                                  const anlz_metadata_t *meta);
#ifndef WIN32
typedef struct {
    int index;
    uint8_t deck;
    uint32_t generation;
    media_source_t source;
    media_catalog_track_t item;
    media_loaded_track_t loaded;
    esp_err_t rc;
    char status[40];
} ui_track_load_result_t;

typedef struct {
    int index;
    uint8_t deck;
    uint32_t generation;
    media_source_t source;
} ui_track_load_request_t;

static ui_track_load_result_t s_track_load_worker_result;

static void ui_submit_track_load(int index, uint8_t deck);
static void ui_poll_track_load_result(void);
#endif

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
    memset(s_cache_beat_dots, 0, sizeof(s_cache_beat_dots));
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

static void ui_library_set_load_busy(bool busy, const char *hint)
{
    lv_obj_t *buttons[] = { s_btn_library_load, s_btn_library_load_deck2 };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        lv_obj_t *btn = buttons[i];
        if (!btn) continue;
        if (busy) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
            lv_obj_add_style(btn, &s_style_btn_disabled, LV_PART_MAIN);
        } else {
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
            lv_obj_remove_style(btn, &s_style_btn_disabled, LV_PART_MAIN);
        }
    }

    if (s_label_library_hint) {
        lv_label_set_text(s_label_library_hint, hint ? hint : "SELECT TRACK\nLOAD D1/D2");
    }
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
    if (s_label_loop_status) {
        if (s_loop_active && s_loop_active_beats > 0) {
            lv_label_set_text_fmt(s_label_loop_status, "ACTIVE: %d BEATS", s_loop_active_beats);
            lv_obj_set_style_text_color(s_label_loop_status, COL_RED, LV_PART_MAIN);
        } else if (s_loop_active) {
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

        bool is_active = s_loop_active && s_loop_active_beats == loop_beats[i];
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

static void ui_update_performance_target_visuals(void)
{
    uint8_t active_deck = ui_performance_target_get(&s_performance_target);

    for (size_t i = 0; i < s_perf_target_button_count; i++) {
        lv_obj_t *btn = s_perf_target_buttons[i];
        if (!btn) {
            continue;
        }
        uint8_t deck = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
        bool active = deck == active_deck;
        lv_color_t color = deck == CTRL_DECK_1 ? COL_ACCENT : COL_GREEN;
        lv_obj_set_style_bg_color(btn, active ? color : COL_PANEL_DK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, active ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, color, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, active ? 2 : 1, LV_PART_MAIN);

        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, active ? COL_ON_ACCENT : color, LV_PART_MAIN);
        }
    }

    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        ui_overview_deck_panel_t *panel = &s_overview_decks[deck];
        if (!panel->panel) {
            continue;
        }
        bool active = deck == active_deck;
        lv_obj_set_style_border_width(panel->panel, 0, LV_PART_MAIN);
        if (panel->label_deck) {
            lv_obj_set_style_text_color(panel->label_deck, active ? COL_TEXT : COL_TEXT_MUTED, LV_PART_MAIN);
            lv_obj_set_style_border_color(panel->label_deck, active ? COL_TEXT : COL_BORDER, LV_PART_MAIN);
            lv_obj_set_style_border_width(panel->label_deck, active ? 1 : 0, LV_PART_MAIN);
        }
    }
}

static void perf_target_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t deck = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    ui_set_performance_deck(deck);
}

static void overview_deck_select_event_cb(lv_event_t *e)
{
    lv_obj_t *panel = lv_event_get_target(e);
    uint8_t deck = (uint8_t)(uintptr_t)lv_obj_get_user_data(panel);
    ui_set_performance_deck(deck);
}

static void ui_create_performance_target_selector(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_label_set_small_caps(label, "TARGET", COL_TEXT_MUTED);
    lv_obj_set_pos(label, x, y + 8);

    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_remove_style_all(btn);
        lv_obj_add_style(btn, &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 52, 34);
        lv_obj_set_pos(btn, x + 70 + deck * 60, y);
        lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)deck);
        lv_obj_add_event_cb(btn, perf_target_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, deck == CTRL_DECK_1 ? "D1" : "D2");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        if (s_perf_target_button_count < sizeof(s_perf_target_buttons) / sizeof(s_perf_target_buttons[0])) {
            s_perf_target_buttons[s_perf_target_button_count++] = btn;
        }
    }

    ui_update_performance_target_visuals();
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

static int ui_media_count(void)
{
#ifndef WIN32
    return media_catalog_count();
#else
    return library_count();
#endif
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
#ifndef WIN32
    uint8_t idx = ui_deck_index(deck);
    if (s_loaded_media_valid[idx]) return s_loaded_media[idx].duration_ms;
#endif
    if (deck != CTRL_DECK_1) return 0;
    const library_track_t *track = library_get_ptr(mock_library_get_current_track_index());
    return track ? track->duration_ms : 0;
}

static uint16_t ui_deck_bpm(uint8_t deck)
{
#ifndef WIN32
    uint8_t idx = ui_deck_index(deck);
    if (s_loaded_media_valid[idx] && s_loaded_media[idx].bpm > 0) return s_loaded_media[idx].bpm;
#endif
    if (deck != CTRL_DECK_1) return 120;
    const library_track_t *track = library_get_ptr(mock_library_get_current_track_index());
    return track ? track->bpm : 120;
}

static uint16_t ui_performance_bpm(void)
{
    return ui_deck_bpm(ui_performance_target_get(&s_performance_target));
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
    return ui_deck_anlz(ui_performance_target_get(&s_performance_target));
}

static deck_state_t ui_performance_deck_state(void)
{
    uint8_t deck = ui_performance_target_get(&s_performance_target);
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
    s_loop_active_by_deck[idx] = active;
    s_loop_start_ms_by_deck[idx] = start_ms;
    s_loop_end_ms_by_deck[idx] = end_ms;
    s_loop_active_beats_by_deck[idx] = beats;

    if (ui_performance_target_is_active(&s_performance_target, idx)) {
        s_loop_active = active;
        s_loop_start_ms = start_ms;
        s_loop_end_ms = end_ms;
        s_loop_active_beats = beats;
        ui_update_loop_screen_state();
    }
}

static void ui_refresh_loop_screen_from_target(void)
{
    uint8_t deck = ui_performance_target_get(&s_performance_target);
    s_loop_active = s_loop_active_by_deck[deck];
    s_loop_start_ms = s_loop_start_ms_by_deck[deck];
    s_loop_end_ms = s_loop_end_ms_by_deck[deck];
    s_loop_active_beats = s_loop_active_beats_by_deck[deck];
    ui_update_loop_screen_state();
}

static void ui_set_performance_deck(uint8_t deck)
{
    uint8_t before = ui_performance_target_get(&s_performance_target);
    ui_performance_target_set(&s_performance_target, ui_deck_index(deck));
    uint8_t after = ui_performance_target_get(&s_performance_target);
    if (before != after) {
        ui_invalidate_header_cache();
    }

    ui_update_performance_target_visuals();
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

static void ui_update_library_source_label(void)
{
    if (!s_label_library_source) {
        return;
    }
#ifndef WIN32
    if (media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK) {
        lv_label_set_text_fmt(s_label_library_source, "JOINED  %d TRACKS", media_catalog_count());
    } else {
        lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", media_catalog_count());
    }
#else
    lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", library_count());
#endif
}

#ifndef WIN32
static void ui_track_load_worker(void *arg)
{
    ui_track_load_request_t req = *(ui_track_load_request_t *)arg;
    free(arg);

    ui_track_load_result_t *result = &s_track_load_worker_result;
    memset(result, 0, sizeof(*result));
    result->index = req.index;
    result->deck = req.deck;
    result->generation = req.generation;
    result->source = req.source;
    result->rc = ESP_OK;

    if (media_catalog_get(req.index, &result->item) != ESP_OK) {
        result->rc = ESP_ERR_NOT_FOUND;
        snprintf(result->status, sizeof(result->status), "NO TRACK");
    } else {
        result->rc = media_catalog_load(req.index, &result->loaded);
        if (result->rc != ESP_OK) {
            const char *status = (req.source == MEDIA_SOURCE_REMOTE_LINK) ? remote_cache_status() : "LOAD ERR";
            snprintf(result->status, sizeof(result->status), "%s", status && status[0] ? status : "LOAD ERR");
        } else {
            if (req.deck == CTRL_DECK_1) {
                audio_engine_clear_loop();
            }
            deck_core_reset_deck(req.deck);
            result->rc = audio_engine_deck_load(req.deck,
                                                result->loaded.audio_path,
                                                result->loaded.has_pvbr ? result->loaded.pvbr : NULL,
                                                result->loaded.duration_ms);
            if (result->rc != ESP_OK) {
                const char *audio_err = audio_engine_last_error_text();
                snprintf(result->status, sizeof(result->status), "%s",
                         audio_err && audio_err[0] ? audio_err : "AUDIO ERR");
            } else {
                snprintf(result->status, sizeof(result->status), "TRACK LOADED");
            }
        }
    }

    if (s_track_load_result_q) {
        xQueueOverwrite(s_track_load_result_q, result);
    }
    ESP_LOGI(TAG, "ui_load stack high water=%u words",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

static void ui_submit_track_load(int index, uint8_t deck)
{
    if (!s_track_load_result_q) {
        s_track_load_result_q = xQueueCreate(1, sizeof(ui_track_load_result_t));
    }
    if (!s_track_load_result_q) {
        ui_status_indicator_hold("NO QUEUE", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO QUEUE");
        s_track_load_busy = false;
        return;
    }

    ui_track_load_result_t stale;
    while (xQueueReceive(s_track_load_result_q, &stale, 0) == pdTRUE) {
    }

    ui_track_load_request_t *req = malloc(sizeof(*req));
    if (!req) {
        ui_status_indicator_hold("NO MEM", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO MEM");
        s_track_load_busy = false;
        return;
    }
    req->index = index;
    req->deck = deck;
    req->generation = library_generation();
    req->source = media_catalog_get_source();

    if (xTaskCreate(ui_track_load_worker, "ui_load", UI_TRACK_LOAD_STACK, req, 3, NULL) != pdPASS) {
        free(req);
        ui_status_indicator_hold("NO TASK", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO TASK");
        s_track_load_busy = false;
    }
}

static void ui_apply_usb_removed(void)
{
    s_usb_removed_pending = false;
    bool removed_loaded = false;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        if (s_loaded_media_valid[deck] && s_loaded_media_source[deck] == MEDIA_SOURCE_LOCAL_USB) {
            s_loaded_media_valid[deck] = false;
            ui_deck_anlz_store_clear(&s_deck_anlz_store, deck);
            ui_deck_track_info_clear(deck);
            ui_set_loop_shadow(deck, false, 0, 0, 0);
            ui_load_waveform_data(deck, 0, NULL, false, NULL);
            removed_loaded = true;
        }
    }
    if (removed_loaded) {
        ui_cache_invalidate();
        lv_label_set_text(s_label_title, "No Track");
        lv_label_set_text(s_label_artist, "");
        ui_label_set_f2(s_label_bpm, 0.0f);
        s_loop_active = false;
        s_loop_active_beats = 0;
        ui_update_loop_screen_state();
        ui_update_hot_cues();
        ui_status_indicator_hold("USB REMOVED", COL_AMBER, 2500);
    }
    if (media_catalog_get_source() == MEDIA_SOURCE_LOCAL_USB) {
        ui_library_set_load_busy(false, "USB REMOVED");
        s_track_load_busy = false;
    }
}

static void ui_poll_track_load_result(void)
{
    if (!s_track_load_result_q) return;

    ui_track_load_result_t result;
    while (xQueueReceive(s_track_load_result_q, &result, 0) == pdTRUE) {
        bool stale = (result.source != media_catalog_get_source());
        if (result.source == MEDIA_SOURCE_LOCAL_USB &&
            result.generation != library_generation()) {
            stale = true;
        }
        if (stale) {
            s_track_load_busy = false;
            ui_library_set_load_busy(false,
                                     result.source == MEDIA_SOURCE_LOCAL_USB ? "USB REMOVED" : "STALE");
            continue;
        }

        if (result.rc != ESP_OK) {
            const char *display = result.status[0] ? result.status : "LOAD ERR";
            ESP_LOGW(TAG, "track load worker failed index=%d: %s", result.index, esp_err_to_name(result.rc));
            ui_status_indicator_hold(display, ui_status_color_for_text(display), 3500);
            ui_library_set_load_busy(false, display);
            s_track_load_busy = false;
            continue;
        }

        if (result.source == MEDIA_SOURCE_LOCAL_USB) {
            mock_library_load_track_to_deck(result.index);
        }
        uint8_t deck = ui_deck_index(result.deck);
        s_loaded_media[deck] = result.loaded;
        s_loaded_media_valid[deck] = true;
        s_loaded_media_source[deck] = result.source;
        ui_deck_track_info_set(deck,
                               result.item.title,
                               result.item.artist,
                               result.loaded.bpm ? result.loaded.bpm : result.item.bpm,
                               result.loaded.duration_ms);
        ui_deck_anlz_set_from_current(deck, media_catalog_get_loaded_anlz_for_source(result.source));
        ui_load_waveform_media(deck, &result.loaded);
        ui_set_loop_shadow(deck, false, 0, 0, 0);

        if (deck == CTRL_DECK_1) {
            ui_cache_invalidate();
            lv_label_set_text(s_label_title, result.item.title[0] ? result.item.title : "Unknown Title");
            lv_label_set_text(s_label_artist, result.item.artist[0] ? result.item.artist : "Unknown Artist");
            ui_label_set_f2(s_label_bpm, (float)(result.loaded.bpm ? result.loaded.bpm : result.item.bpm));
        }
        if (ui_performance_target_is_active(&s_performance_target, deck)) {
            ui_update_hot_cues();
        }

        ESP_LOGI(TAG, "Audio: loaded deck %u: %s (autoplay off)",
                 (unsigned)result.deck + 1u, result.loaded.audio_path);
        const char *loaded_text = result.deck == CTRL_DECK_1 ? "D1 LOADED" : "D2 PRODUCER";
        ui_status_indicator_hold(loaded_text, COL_GREEN, 2000);
        ui_library_set_load_busy(false, loaded_text);
        s_track_load_busy = false;
    }
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

static uint8_t ui_event_deck(lv_event_t *e)
{
    if (!e) return CTRL_DECK_1;
    lv_obj_t *target = lv_event_get_target(e);
    return ui_deck_index((uint8_t)(uintptr_t)lv_obj_get_user_data(target));
}

static uint8_t ui_deck_control_id(uint8_t deck, uint8_t deck1_id, uint8_t deck2_id)
{
    return ui_deck_index(deck) == CTRL_DECK_2 ? deck2_id : deck1_id;
}

// Play/Pause button on overview clicked
static void play_pause_event_cb(lv_event_t *e) {
    uint8_t deck = ui_event_deck(e);
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

// CUE button on overview clicked. Returns to the cue point (track start by
// default) and pauses — handled in deck_core on firmware (BTN_CUE).
static void cue_event_cb(lv_event_t *e) {
    uint8_t deck = ui_event_deck(e);
#ifdef WIN32
    (void)deck;
    // Simulator: mirror deck_core — return to the cue point (start) and pause.
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

// Load button in library clicked
static void library_load_event_cb(lv_event_t *e) {
    uint8_t deck = s_library_load_request_deck;
    if (e) {
        lv_obj_t *btn = lv_event_get_target(e);
        deck = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    }
    if (s_track_load_busy) {
        ui_status_indicator_hold("LOAD BUSY", COL_AMBER, 1200);
        return;
    }
    s_track_load_busy = true;
    ui_library_set_load_busy(true, "LOAD BUSY");

#ifdef WIN32
    library_track_t *track = library_get_ptr(s_selected_track_idx);
    if (!track) {
        ui_library_set_load_busy(false, NULL);
        s_track_load_busy = false;
        return;
    }

    mock_library_load_track_to_deck(s_selected_track_idx);
    library_load_anlz(track);          // load BPM, duration, waveform from ANLZ

    /* Load detailed ANLZ metadata for the active track */
    library_load_current_anlz(track);

    ui_deck_track_info_set(deck, track->title, track->artist, track->bpm, track->duration_ms);
    ui_deck_anlz_set_from_current(deck, library_get_current_anlz());
    ui_load_waveform_data(deck, track->duration_ms, track->waveform_low,
                          track->has_waveform != 0,
                          ui_deck_anlz(deck));

    if (deck == CTRL_DECK_1) {
        lv_label_set_text(s_label_title, track->title);
        lv_label_set_text(s_label_artist, track->artist);
        ui_label_set_f2(s_label_bpm, (float)track->bpm);
    }
    ui_set_loop_shadow(deck, false, 0, 0, 0);

    /* Populate hot cue points from active ANLZ metadata */
    if (ui_performance_target_is_active(&s_performance_target, deck)) {
        ui_update_hot_cues();
    }

    ESP_LOGI(TAG, "Loaded track to deck %u: %s by %s (waveform=%d)",
             (unsigned)deck + 1u, track->title, track->artist, track->has_waveform);
#else
    media_catalog_track_t item;
    if (media_catalog_get(s_selected_track_idx, &item) != ESP_OK) {
        ESP_LOGW(TAG, "No catalog row at index %d", s_selected_track_idx);
        ui_library_set_load_busy(false, NULL);
        s_track_load_busy = false;
        return;
    }

    const bool remote_source = (media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK);
    ui_status_indicator_hold(remote_source ? "CACHE START" : "LOADING", COL_ACCENT, 1500);
    ui_submit_track_load(s_selected_track_idx, deck);
    return;
#endif
    ui_status_indicator_hold("TRACK LOADED", COL_GREEN, 2000);
    ui_library_set_load_busy(false, "TRACK LOADED");
    s_track_load_busy = false;
}

bool ui_is_library_active(void)
{
    return s_active_tab == 1 && s_library_table != NULL;
}

esp_err_t ui_library_select_delta(int delta)
{
    if (delta == 0) {
        return ESP_OK;
    }
    if (!s_library_table) {
        return ESP_ERR_INVALID_STATE;
    }

    ui_lvgl_lock();
    int n = ui_media_count();
    if (n <= 0) {
        ui_lvgl_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    int new_idx = s_selected_track_idx + delta;
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= n) new_idx = n - 1;
    if (new_idx == s_selected_track_idx) {
        lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
        ui_lvgl_unlock();
        return ESP_OK;
    }

    int old_idx = s_selected_track_idx;
    s_selected_track_idx = new_idx;
    ui_fill_library_row(old_idx);
    ui_fill_library_row(new_idx);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
    ui_lvgl_unlock();
    return ESP_OK;
}

esp_err_t ui_library_load_selected(void)
{
    return ui_library_load_selected_for_deck(CTRL_DECK_1);
}

esp_err_t ui_library_load_selected_for_deck(uint8_t deck)
{
    if (!s_library_table) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ui_media_count() <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_track_load_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    ui_lvgl_lock();
    uint8_t old_deck = s_library_load_request_deck;
    s_library_load_request_deck = deck;
    library_load_event_cb(NULL);
    s_library_load_request_deck = old_deck;
    ui_lvgl_unlock();
    return ESP_OK;
}

// Sort tracks by Artist (Toggle ASC/DESC)
static void library_sort_artist_event_cb(lv_event_t *e) {
    (void)e;
    if (!s_library_table) return;

#ifdef WIN32
    library_track_t *sel_track = library_get_ptr(s_selected_track_idx);
    uint32_t target_id = sel_track ? sel_track->track_id : 0;

    s_sort_artist_desc = !s_sort_artist_desc;
    library_sort(0, s_sort_artist_desc);
    ui_refresh_library();

    if (target_id != 0) {
        int n = library_count();
        for (int i = 0; i < n; i++) {
            library_track_t *t = library_get_ptr(i);
            if (t && t->track_id == target_id) {
                s_selected_track_idx = i;
                break;
            }
        }
    }
#else
    media_catalog_row_t sel_track;
    uint32_t target_key = (media_catalog_get_row(s_selected_track_idx, &sel_track) == ESP_OK)
                          ? sel_track.track_key : 0;

    s_sort_artist_desc = !s_sort_artist_desc;
    media_catalog_sort(0, s_sort_artist_desc);
    ui_refresh_library();

    if (target_key != 0) {
        int n = media_catalog_count();
        for (int i = 0; i < n; i++) {
            media_catalog_row_t t;
            if (media_catalog_get_row(i, &t) == ESP_OK && t.track_key == target_key) {
                s_selected_track_idx = i;
                break;
            }
        }
    }
#endif
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
}

// Sort tracks by Title/Name (Toggle ASC/DESC)
static void library_sort_name_event_cb(lv_event_t *e) {
    (void)e;
    if (!s_library_table) return;

#ifdef WIN32
    library_track_t *sel_track = library_get_ptr(s_selected_track_idx);
    uint32_t target_id = sel_track ? sel_track->track_id : 0;

    s_sort_name_desc = !s_sort_name_desc;
    library_sort(1, s_sort_name_desc);
    ui_refresh_library();

    if (target_id != 0) {
        int n = library_count();
        for (int i = 0; i < n; i++) {
            library_track_t *t = library_get_ptr(i);
            if (t && t->track_id == target_id) {
                s_selected_track_idx = i;
                break;
            }
        }
    }
#else
    media_catalog_row_t sel_track;
    uint32_t target_key = (media_catalog_get_row(s_selected_track_idx, &sel_track) == ESP_OK)
                          ? sel_track.track_key : 0;

    s_sort_name_desc = !s_sort_name_desc;
    media_catalog_sort(1, s_sort_name_desc);
    ui_refresh_library();

    if (target_key != 0) {
        int n = media_catalog_count();
        for (int i = 0; i < n; i++) {
            media_catalog_row_t t;
            if (media_catalog_get_row(i, &t) == ESP_OK && t.track_key == target_key) {
                s_selected_track_idx = i;
                break;
            }
        }
    }
#endif
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
}

// Sort tracks by BPM (Toggle ASC/DESC)
static void library_sort_bpm_event_cb(lv_event_t *e) {
    (void)e;
    if (!s_library_table) return;

#ifdef WIN32
    library_track_t *sel_track = library_get_ptr(s_selected_track_idx);
    uint32_t target_id = sel_track ? sel_track->track_id : 0;

    s_sort_bpm_desc = !s_sort_bpm_desc;
    library_sort(2, s_sort_bpm_desc);
    ui_refresh_library();

    if (target_id != 0) {
        int n = library_count();
        for (int i = 0; i < n; i++) {
            library_track_t *t = library_get_ptr(i);
            if (t && t->track_id == target_id) {
                s_selected_track_idx = i;
                break;
            }
        }
    }
#else
    media_catalog_row_t sel_track;
    uint32_t target_key = (media_catalog_get_row(s_selected_track_idx, &sel_track) == ESP_OK)
                          ? sel_track.track_key : 0;

    s_sort_bpm_desc = !s_sort_bpm_desc;
    media_catalog_sort(2, s_sort_bpm_desc);
    ui_refresh_library();

    if (target_key != 0) {
        int n = media_catalog_count();
        for (int i = 0; i < n; i++) {
            media_catalog_row_t t;
            if (media_catalog_get_row(i, &t) == ESP_OK && t.track_key == target_key) {
                s_selected_track_idx = i;
                break;
            }
        }
    }
#endif
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
}

// Select track row in library table
static void library_table_event_cb(lv_event_t *e) {
    lv_obj_t *table = lv_event_get_target(e);
    uint32_t row;
    uint32_t col;
    lv_table_get_selected_cell(table, &row, &col);

    if (row > 0 && (int)row <= ui_media_count()) {
        int new_idx = (int)row - 1;
        if (new_idx != s_selected_track_idx) {
            int old_idx = s_selected_track_idx;
            s_selected_track_idx = new_idx;
            ui_fill_library_row(old_idx);
            ui_fill_library_row(new_idx);
            // Explicitly restore the selection, since updating cell values resets the selection in LVGL
            lv_table_set_selected_cell(table, row, 0);
            ESP_LOGD(TAG, "Library selected track index: %d", s_selected_track_idx);
        }
    }
}

// Trigger hot cue pads
static void hot_cue_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int cue_idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    uint32_t pos = s_hot_cue_positions[cue_idx];
    uint8_t deck = ui_performance_target_get(&s_performance_target);

    if (pos == 0xFFFFFFFF) {
        ESP_LOGI(TAG, "D%u Hot Cue %c is empty, ignoring click",
                 (unsigned)deck + 1u, 'A' + cue_idx);
        return;
    }

    uint8_t type = s_hot_cue_types[cue_idx];
    uint32_t end_pos = s_hot_cue_ends[cue_idx];

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
    uint8_t deck = ui_performance_target_get(&s_performance_target);

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
    uint8_t deck = ui_performance_target_get(&s_performance_target);
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
    uint8_t deck = ui_performance_target_get(&s_performance_target);

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

static void library_source_local_event_cb(lv_event_t *e)
{
    (void)e;
#ifndef WIN32
    media_catalog_set_source(MEDIA_SOURCE_LOCAL_USB);
#endif
    s_selected_track_idx = 0;
    ui_update_library_source_label();
    ui_refresh_library();
}

static void library_source_joined_event_cb(lv_event_t *e)
{
    (void)e;
#ifndef WIN32
    esp_err_t rc = cdj_link_client_start();
    if (rc == ESP_OK) {
        rc = media_catalog_refresh_remote();
    }
    if (rc == ESP_OK) {
        media_catalog_set_source(MEDIA_SOURCE_REMOTE_LINK);
        s_selected_track_idx = 0;
        ui_status_indicator_hold("JOINED", COL_GREEN, 2000);
    } else {
        ui_status_indicator_hold("JOIN FAILED", COL_RED, 3500);
        ESP_LOGW(TAG, "joined library refresh failed: %s", esp_err_to_name(rc));
    }
#endif
    ui_update_library_source_label();
    ui_refresh_library();
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
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        if (s_loaded_media_valid[deck] && s_loaded_media_source[deck] == MEDIA_SOURCE_REMOTE_LINK) {
            ui_status_indicator_hold("REMOTE LOADED", COL_AMBER, 2000);
            sd_diag_log_write("sd_cache", "clear blocked while remote track is loaded");
            return;
        }
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

// Screen 1: OVERVIEW Layout
// Tap-to-seek on the overview waveform: jump playback to the tapped position,
// preserving the current play/pause state. The mapping mirrors the playhead —
// the bars span 400 px starting 10 px into wv_border's content area.
static void waveform_seek_event_cb(lv_event_t *e) {
    lv_obj_t *wv = lv_event_get_target(e);
    uint8_t deck = ui_event_deck(e);
    uint32_t duration_ms = ui_deck_duration_ms(deck);
    if (duration_ms == 0) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t content;
    lv_obj_get_content_coords(wv, &content);
    int rel_x = (int)p.x - content.x1 - 10;
    if (rel_x < 0)   rel_x = 0;
    if (rel_x > OVERVIEW_CV_W) rel_x = OVERVIEW_CV_W;
    uint32_t target_ms = (uint32_t)(((uint64_t)rel_x * duration_ms) / OVERVIEW_CV_W);

#ifndef WIN32
    audio_engine_deck_seek(deck, target_ms);
#else
    (void)deck;
    mock_deck_set_position(target_ms);
#endif
    ESP_LOGI(TAG, "D%u waveform seek -> %lu ms (%d%%)",
             (unsigned)deck + 1u, (unsigned long)target_ms, (int)((rel_x * 100) / OVERVIEW_CV_W));
}

static lv_obj_t *ui_overview_value_label(lv_obj_t *parent, const lv_font_t *font,
                                         lv_color_t color, int x, int y,
                                         int w, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(label, w, 24);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *ui_overview_compact_button(lv_obj_t *parent, uint8_t deck,
                                            int x, int y, int w, const char *text,
                                            const lv_style_t *style,
                                            lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, style, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, w, 34);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_user_data(btn, (void *)(uintptr_t)deck);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return btn;
}

static lv_obj_t *ui_overview_bar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_style_bg_color(bar, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

#ifndef WIN32
static void *ui_overview_alloc_canvas(size_t size, bool prefer_psram)
{
    void *buf = NULL;
    if (prefer_psram) {
        buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}
#endif

static void ui_create_overview_deck_panel(lv_obj_t *parent, uint8_t deck, int y)
{
    (void)y;
    ui_overview_deck_panel_t *panel = &s_overview_decks[ui_deck_index(deck)];
    panel->wave_stride_px = OVERVIEW_CV_W;
    panel->mini_wave_stride_px = OVERVIEW_MINI_CV_W;
    panel->last_fill_x = -1;
    panel->last_mini_fill_x = -1;
    panel->last_playhead_x = -1;
    int top_y = (deck == CTRL_DECK_1) ? 0 : 158;
    int info_x = (deck == CTRL_DECK_1) ? 0 : 400;
    int accent_x = info_x + 2;

    panel->panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel->panel);
    lv_obj_add_style(panel->panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel->panel, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel->panel, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->panel, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->panel, UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(panel->panel, 0, 0);
    lv_obj_set_style_pad_all(panel->panel, 0, LV_PART_MAIN);
    lv_obj_set_user_data(panel->panel, (void *)(uintptr_t)deck);
    lv_obj_remove_flag(panel->panel, LV_OBJ_FLAG_CLICKABLE);

    panel->label_deck = ui_overview_value_label(panel->panel, &lv_font_montserrat_18,
                                                COL_TEXT, 0, top_y + 4, 88,
                                                deck == CTRL_DECK_1 ? "DECK 1" : "DECK 2");
    lv_obj_set_style_bg_color(panel->label_deck, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_deck, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel->label_deck, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel->label_deck, 2, LV_PART_MAIN);
    lv_obj_set_user_data(panel->label_deck, (void *)(uintptr_t)deck);
    lv_obj_add_flag(panel->label_deck, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel->label_deck, overview_deck_select_event_cb, LV_EVENT_CLICKED, NULL);
    panel->label_status = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                                  COL_RED, 4, top_y + 36, 90, "((PAUSE))");
    panel->label_title = ui_overview_value_label(panel->panel, &lv_font_montserrat_16,
                                                 COL_TEXT, info_x, 316, 400, "No Track");
    lv_obj_set_style_bg_color(panel->label_title, COL_TITLE_BLUE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_title, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel->label_title, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel->label_title, 1, LV_PART_MAIN);
    panel->label_artist = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                                  COL_TEXT_MUTED, info_x + 28, 350, 112, "TRACK");
    panel->label_time = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                COL_TEXT, info_x + 78, 374, 150, "00:00.00");
    panel->label_remain = ui_overview_value_label(panel->panel, &lv_font_montserrat_16,
                                                  COL_TEXT_MUTED, info_x + 228, 382, 86, "-00:00");
    panel->label_bpm = ui_overview_value_label(panel->panel, &lv_font_montserrat_14,
                                               COL_TEXT, info_x + 334, 366, 62, "120.00");
    panel->label_pitch = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                                 COL_TEXT_MUTED, info_x + 284, 382, 48, "0.0%");

    ui_overview_bar(panel->panel, info_x, 340, 400, 1, COL_BORDER);
    ui_overview_bar(panel->panel, accent_x, 423, 396, 10,
                    deck == CTRL_DECK_1 ? COL_RED : COL_ACCENT);
    ui_overview_value_label(panel->panel, &lv_font_montserrat_12, COL_TEXT_MUTED,
                            info_x + 372, 352, 26, "BPM");

    panel->label_ch = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                              COL_TEXT_MUTED, info_x + 10, 374, 60, "CH 100%");
    panel->label_out = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                               COL_TEXT_MUTED, info_x + 10, 394, 68, "OUT 100%");
    panel->label_pfl = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                               COL_TEXT_DIM, 4, top_y + 100, 80, "PFL OFF");
    panel->out_bar_bg = ui_overview_bar(panel->panel, info_x + 286, 410, 78, 6, COL_PANEL);
    panel->out_bar_fill = ui_overview_bar(panel->panel, info_x + 286, 410, 78, 6,
                                          deck == CTRL_DECK_1 ? COL_ACCENT : COL_GREEN);

    panel->wave_border = lv_obj_create(panel->panel);
    lv_obj_remove_style_all(panel->wave_border);
    lv_obj_add_style(panel->wave_border, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel->wave_border, lv_color_hex(0x020406), LV_PART_MAIN);
    lv_obj_set_size(panel->wave_border, OVERVIEW_CV_W + 20, OVERVIEW_CV_H + 18);
    lv_obj_set_pos(panel->wave_border, 98, top_y + 42);
    lv_obj_set_style_pad_all(panel->wave_border, 0, LV_PART_MAIN);
    lv_obj_set_user_data(panel->wave_border, (void *)(uintptr_t)deck);
    lv_obj_remove_flag(panel->wave_border, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel->wave_border, waveform_seek_event_cb, LV_EVENT_CLICKED, NULL);

    size_t ov_sz = LV_DRAW_BUF_SIZE(OVERVIEW_CV_W, OVERVIEW_CV_H, LV_COLOR_FORMAT_I8);
#ifndef WIN32
    panel->wave_buf = ui_overview_alloc_canvas(ov_sz, false);
#else
    panel->wave_buf = malloc(ov_sz);
#endif
    if (panel->wave_buf) {
        memset(panel->wave_buf, 0, ov_sz);
        panel->wave_canvas = lv_canvas_create(panel->wave_border);
        lv_canvas_set_buffer(panel->wave_canvas, panel->wave_buf, OVERVIEW_CV_W, OVERVIEW_CV_H, LV_COLOR_FORMAT_I8);
        lv_obj_align(panel->wave_canvas, LV_ALIGN_TOP_LEFT, 10, 7);
        lv_obj_remove_flag(panel->wave_canvas, LV_OBJ_FLAG_CLICKABLE);

        lv_canvas_set_palette(panel->wave_canvas, 0, lv_color32_make(0x00, 0x00, 0x00, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 1, lv_color32_make(0x00, 0x7D, 0xE1, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 2, lv_color32_make(0xA6, 0xC8, 0xE8, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 3, lv_color32_make(0x5F, 0x5F, 0x5F, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 4, lv_color32_make(0xE5, 0xE6, 0xEA, 0xFF));

        lv_image_dsc_t *dsc = lv_canvas_get_image(panel->wave_canvas);
        if (dsc && dsc->header.stride > 0) panel->wave_stride_px = (int)dsc->header.stride;
    } else {
        ESP_LOGE(TAG, "D%u overview canvas buffer alloc failed (%u bytes)",
                 (unsigned)deck + 1u, (unsigned)ov_sz);
    }

    panel->playhead = lv_obj_create(panel->wave_border);
    lv_obj_set_style_bg_color(panel->playhead, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->playhead, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->playhead, 3, OVERVIEW_CV_H);
    lv_obj_set_pos(panel->playhead, 10, 7);
    lv_obj_remove_flag(panel->playhead, LV_OBJ_FLAG_CLICKABLE);

    panel->mini_wave_border = lv_obj_create(panel->panel);
    lv_obj_remove_style_all(panel->mini_wave_border);
    lv_obj_set_style_bg_color(panel->mini_wave_border, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->mini_wave_border, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->mini_wave_border, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->mini_wave_border, OVERVIEW_MINI_CV_W, OVERVIEW_MINI_CV_H);
    lv_obj_set_pos(panel->mini_wave_border, info_x + 4, 401);
    lv_obj_remove_flag(panel->mini_wave_border, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(panel->mini_wave_border, LV_OBJ_FLAG_SCROLLABLE);

    size_t mini_sz = LV_DRAW_BUF_SIZE(OVERVIEW_MINI_CV_W, OVERVIEW_MINI_CV_H, LV_COLOR_FORMAT_I8);
#ifndef WIN32
    panel->mini_wave_buf = ui_overview_alloc_canvas(mini_sz, true);
#else
    panel->mini_wave_buf = malloc(mini_sz);
#endif
    if (panel->mini_wave_buf) {
        memset(panel->mini_wave_buf, 0, mini_sz);
        panel->mini_wave_canvas = lv_canvas_create(panel->mini_wave_border);
        lv_canvas_set_buffer(panel->mini_wave_canvas, panel->mini_wave_buf,
                             OVERVIEW_MINI_CV_W, OVERVIEW_MINI_CV_H, LV_COLOR_FORMAT_I8);
        lv_obj_align(panel->mini_wave_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_remove_flag(panel->mini_wave_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_canvas_set_palette(panel->mini_wave_canvas, 0, lv_color32_make(0x00, 0x00, 0x00, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 1, lv_color32_make(0xE8, 0x2B, 0x78, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 2, lv_color32_make(0xFF, 0xA6, 0xD4, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 3, lv_color32_make(0x46, 0xE9, 0xE5, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 4, lv_color32_make(0xE5, 0xE6, 0xEA, 0xFF));
        lv_image_dsc_t *mini_dsc = lv_canvas_get_image(panel->mini_wave_canvas);
        if (mini_dsc && mini_dsc->header.stride > 0) {
            panel->mini_wave_stride_px = (int)mini_dsc->header.stride;
        }
    } else {
        ESP_LOGE(TAG, "D%u mini overview canvas alloc failed (%u bytes)",
                 (unsigned)deck + 1u, (unsigned)mini_sz);
    }

    panel->mini_playhead = lv_obj_create(panel->mini_wave_border);
    lv_obj_set_style_bg_color(panel->mini_playhead, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->mini_playhead, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->mini_playhead, 2, OVERVIEW_MINI_CV_H);
    lv_obj_set_pos(panel->mini_playhead, 0, 0);
    lv_obj_remove_flag(panel->mini_playhead, LV_OBJ_FLAG_CLICKABLE);

    uint8_t deck_idx = ui_deck_index(deck);
    for (int i = 0; i < 8; i++) {
        s_overview_cue_markers[deck_idx][i] = lv_obj_create(panel->wave_border);
        lv_obj_set_style_border_width(s_overview_cue_markers[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_size(s_overview_cue_markers[deck_idx][i], 3, OVERVIEW_CV_H);
        lv_obj_add_flag(s_overview_cue_markers[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_overview_cue_markers[deck_idx][i], LV_OBJ_FLAG_CLICKABLE);
    }

    if (deck == CTRL_DECK_1) {
        for (int i = 0; i < 4; i++) {
            s_beat_pulses[i] = lv_obj_create(panel->panel);
            lv_obj_set_size(s_beat_pulses[i], 12, 12);
            lv_obj_set_pos(s_beat_pulses[i], 390 + i * 18, 146);
            lv_obj_set_style_radius(s_beat_pulses[i], 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(s_beat_pulses[i], 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_beat_pulses[i], COL_PANEL_DK, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_beat_pulses[i], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_beat_pulses[i], COL_BORDER_LT, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_beat_pulses[i], 1, LV_PART_MAIN);
        }
    }

    ui_overview_compact_button(panel->panel, deck, 4, top_y + 68, 38, "PLAY", &s_style_btn_primary, play_pause_event_cb);
    ui_overview_compact_button(panel->panel, deck, 46, top_y + 68, 38, "CUE", &s_style_btn_amber, cue_event_cb);

    if (deck == CTRL_DECK_1) {
        s_crossfader_label = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                                     COL_TEXT_MUTED, OVERVIEW_XFADER_X - 30, 394, 28, "XF");
        s_crossfader_track_w = 84;
        s_crossfader_track = ui_overview_bar(panel->panel, OVERVIEW_XFADER_X, 410, s_crossfader_track_w, 6,
                                             COL_PANEL);
        s_crossfader_knob = ui_overview_bar(panel->panel, OVERVIEW_XFADER_X + (s_crossfader_track_w / 2) - 3,
                                            405, 6, 16, COL_RED);
    }
    ui_update_performance_target_visuals();
}

static lv_obj_t *ui_fx_panel_label(lv_obj_t *parent, const char *text,
                                   int x, int y, int w,
                                   const lv_font_t *font,
                                   lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(label, w, 22);
    lv_obj_set_pos(label, x, y);
    return label;
}

static void ui_create_overview_fx_panel(lv_obj_t *parent)
{
    s_overview_fx_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_overview_fx_panel);
    lv_obj_add_style(s_overview_fx_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_overview_fx_panel, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overview_fx_panel, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overview_fx_panel, 1, LV_PART_MAIN);
    lv_obj_set_size(s_overview_fx_panel, 128, 316);
    lv_obj_set_pos(s_overview_fx_panel, 672, 0);
    lv_obj_clear_flag(s_overview_fx_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = ui_overview_bar(s_overview_fx_panel, 0, 0, 128, 28, COL_PANEL);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    ui_fx_panel_label(s_overview_fx_panel, "BEAT FX", 0, 6, 128,
                      &lv_font_montserrat_14, COL_TEXT);

    const char *values[] = { "ECHO", "ECHO", "REVERB" };
    const int y0[] = { 30, 120, 210 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *slot = ui_overview_bar(s_overview_fx_panel, 4, y0[i], 120, 58, lv_color_hex(0x263033));
        lv_obj_set_style_border_width(slot, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(slot, COL_BORDER, LV_PART_MAIN);
        lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);

        ui_overview_bar(s_overview_fx_panel, 4, y0[i] + 54, 120, 5,
                        i == 1 ? lv_color_hex(0x146B17) : lv_color_hex(0x18F72B));
        ui_fx_panel_label(s_overview_fx_panel, values[i], 10, y0[i] + 20, 84,
                          &lv_font_montserrat_14, COL_ACCENT);
        ui_fx_panel_label(s_overview_fx_panel, "v", 102, y0[i] + 20, 18,
                          &lv_font_montserrat_14, COL_TEXT);

        lv_obj_t *off = ui_overview_bar(s_overview_fx_panel, 4, y0[i] + 62, 120, 26, COL_BG);
        lv_obj_set_style_border_width(off, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(off, COL_BORDER, LV_PART_MAIN);
        lv_obj_remove_flag(off, LV_OBJ_FLAG_CLICKABLE);
        ui_fx_panel_label(s_overview_fx_panel, "OFF", 0, y0[i] + 68, 128,
                          &lv_font_montserrat_12, COL_TEXT_DIM);
    }
}

static void ui_create_overview_center_marker(lv_obj_t *parent)
{
    lv_obj_t *line = ui_overview_bar(parent, 421, 0, 1, 316, COL_TEXT);
    lv_obj_set_style_bg_opa(line, LV_OPA_80, LV_PART_MAIN);

    lv_obj_t *top = ui_overview_bar(parent, 417, 0, 9, 2, COL_TEXT);
    lv_obj_set_style_bg_opa(top, LV_OPA_80, LV_PART_MAIN);
    lv_obj_t *bottom = ui_overview_bar(parent, 417, 158, 9, 2, COL_TEXT);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_80, LV_PART_MAIN);

    lv_obj_t *cue = ui_overview_bar(parent, 407, 300, 32, 16, COL_AMBER);
    lv_obj_set_style_border_width(cue, 0, LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(cue);
    lv_label_set_text(label, "CUE");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static void create_screen_overview(lv_obj_t *parent) {
    s_screens[0] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[0]);
    lv_obj_add_style(s_screens[0], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[0], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[0], 0, UI_CONTENT_Y);

    ui_create_overview_deck_panel(s_screens[0], CTRL_DECK_1, 4);
    ui_create_overview_deck_panel(s_screens[0], CTRL_DECK_2, 222);
    ui_create_overview_center_marker(s_screens[0]);
    ui_create_overview_fx_panel(s_screens[0]);
}

static void ui_truncate_str(char *dest, const char *src, size_t max_len) {
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len <= max_len) {
        strcpy(dest, src);
    } else {
        strncpy(dest, src, max_len - 3);
        dest[max_len - 3] = '\0';
        strcat(dest, "...");
    }
}

// Fill one library data row (track index i → table row i+1). Caller holds the
// LVGL lock. NOTE: the table must have a fixed row height (min==max on ITEMS),
// otherwise lv_table_set_cell_value triggers an O(n) self-size refresh per row
// and populating a 300+ track table becomes O(n²) (seconds, watchdog trips).
static void ui_fill_library_row(int i) {
#ifndef WIN32
    media_catalog_row_t row;
    if (media_catalog_get_row(i, &row) != ESP_OK) {
        return;
    }
    const char *title = row.title;
    const char *artist = row.artist;
    uint16_t bpm = row.bpm;
    uint32_t duration_ms = row.duration_ms;
#else
    const library_track_t *track = library_get_ptr(i);
    if (!track) {
        return;
    }
    const char *title = track->title;
    const char *artist = track->artist;
    uint16_t bpm = track->bpm;
    uint32_t duration_ms = track->duration_ms;
#endif
    char bpm_str[16];
    char time_str[16];
    char trunc_title[128];
    char trunc_artist[128];

    // Smart truncation for Montserrat 16 font (26 chars for title, 18 for artist)
    ui_truncate_str(trunc_title, title, 26);
    ui_truncate_str(trunc_artist, artist, 18);

    uint32_t secs = duration_ms / 1000;
    snprintf(bpm_str, sizeof(bpm_str), "%u", (unsigned)bpm);
    snprintf(time_str, sizeof(time_str), "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));

    lv_table_set_cell_value(s_library_table, i + 1, 0, trunc_title);
    lv_table_set_cell_value(s_library_table, i + 1, 1, trunc_artist);
    lv_table_set_cell_value(s_library_table, i + 1, 2, bpm_str);
    lv_table_set_cell_value(s_library_table, i + 1, 3, time_str);
}

// (Re)fill all library table rows. Sized exactly to the track count (+1 header)
// so stale rows are dropped. Used at init where the count is small/zero.
static void ui_populate_library_rows(void) {
    if (!s_library_table) {
        return;
    }
    int n_tracks = ui_media_count();
    lv_table_set_row_count(s_library_table, n_tracks + 1);   // row 0 = header
    for (int i = 0; i < n_tracks; i++) {
        ui_fill_library_row(i);
    }
    ui_update_library_source_label();
}

// Screen 2: LIBRARY BROWSER Layout
static void create_screen_library(lv_obj_t *parent) {
    s_screens[1] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[1]);
    lv_obj_add_style(s_screens[1], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[1], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[1], 0, UI_CONTENT_Y);

    // Track list container table
    s_library_table = lv_table_create(s_screens[1]);
    lv_obj_set_size(s_library_table, 600, 330);
    lv_obj_set_pos(s_library_table, 10, 10);
    lv_obj_add_event_cb(s_library_table, library_table_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // 8 visible songs + 1 header = 9 rows total. 330px height / 9 = 36.6px.
    // Fixed row height (36px) allows large legible text without word-wrap clipping.
    lv_obj_set_style_min_height(s_library_table, 36, LV_PART_ITEMS);
    lv_obj_set_style_max_height(s_library_table, 36, LV_PART_ITEMS);

    // Modern compact padding tailored for 36px row and 16px font
    lv_obj_set_style_pad_left(s_library_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_library_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_library_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_library_table, 8, LV_PART_ITEMS);

    // Subtly visible grid borders for tabular layout — draw only at the bottom of cells
    lv_obj_set_style_border_color(s_library_table, COL_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_library_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_library_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);

    // Harmonious background color and premium Montserrat 16 font
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ROW, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_library_table, COL_TEXT_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_library_table, &lv_font_montserrat_16, LV_PART_ITEMS);

    // Selected cell background and text color (Standard LVGL FOCUSED state)
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ALT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_library_table, COL_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(s_library_table, 3, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(s_library_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(s_library_table, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(s_library_table, COL_ON_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);

    // Configure Columns: Total width must fit 600px (4 columns instead of 5, '#' removed)
    lv_table_set_column_width(s_library_table, 0, 290); // Title
    lv_table_set_column_width(s_library_table, 1, 170); // Artist
    lv_table_set_column_width(s_library_table, 2, 60);  // BPM
    lv_table_set_column_width(s_library_table, 3, 80);  // Duration

    // Header Row
    lv_table_set_cell_value(s_library_table, 0, 0, "TITLE");
    lv_table_set_cell_value(s_library_table, 0, 1, "ARTIST");
    lv_table_set_cell_value(s_library_table, 0, 2, "BPM");
    lv_table_set_cell_value(s_library_table, 0, 3, "TIME");

    // Populate rows with track details from the media library
    ui_populate_library_rows();

    // Source selector
    lv_obj_t *btn_src_local = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(btn_src_local);
    lv_obj_add_style(btn_src_local, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_src_local, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_src_local, 72, 38);
    lv_obj_set_pos(btn_src_local, 630, 10);
    lv_obj_add_event_cb(btn_src_local, library_source_local_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_src_local, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_src_local = lv_label_create(btn_src_local);
    lv_label_set_text(lbl_src_local, "LOCAL");
    lv_obj_set_style_text_font(lbl_src_local, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_src_local, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_src_local, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_src_join = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(btn_src_join);
    lv_obj_add_style(btn_src_join, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_src_join, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_src_join, 72, 38);
    lv_obj_set_pos(btn_src_join, 708, 10);
    lv_obj_add_event_cb(btn_src_join, library_source_joined_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_src_join, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_src_join = lv_label_create(btn_src_join);
    lv_label_set_text(lbl_src_join, "JOINED");
    lv_obj_set_style_text_font(lbl_src_join, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_src_join, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_src_join, LV_ALIGN_CENTER, 0, 0);

    s_label_library_source = lv_label_create(s_screens[1]);
    lv_obj_set_style_text_font(s_label_library_source, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_library_source, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(s_label_library_source, 630, 52);
    ui_update_library_source_label();

    // Load selected track buttons (Right Panel)
    s_btn_library_load = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(s_btn_library_load);
    lv_obj_add_style(s_btn_library_load, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load, 72, 50);
    lv_obj_set_pos(s_btn_library_load, 630, 72);
    lv_obj_set_user_data(s_btn_library_load, (void *)(uintptr_t)CTRL_DECK_1);
    lv_obj_add_event_cb(s_btn_library_load, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_load = lv_label_create(s_btn_library_load);
    lv_label_set_text(lbl_load, "LOAD D1");
    lv_obj_set_style_text_font(lbl_load, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_load, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl_load, LV_ALIGN_CENTER, 0, 0);

    s_btn_library_load_deck2 = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(s_btn_library_load_deck2);
    lv_obj_add_style(s_btn_library_load_deck2, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load_deck2, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load_deck2, 72, 50);
    lv_obj_set_pos(s_btn_library_load_deck2, 708, 72);
    lv_obj_set_user_data(s_btn_library_load_deck2, (void *)(uintptr_t)CTRL_DECK_2);
    lv_obj_add_event_cb(s_btn_library_load_deck2, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load_deck2, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_load_deck2 = lv_label_create(s_btn_library_load_deck2);
    lv_label_set_text(lbl_load_deck2, "LOAD D2");
    lv_obj_set_style_text_font(lbl_load_deck2, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_load_deck2, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl_load_deck2, LV_ALIGN_CENTER, 0, 0);

    // SORT ARTIST button
    lv_obj_t *btn_sort_artist = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(btn_sort_artist);
    lv_obj_add_style(btn_sort_artist, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_artist, 150, 45);
    lv_obj_set_pos(btn_sort_artist, 630, 132);
    lv_obj_add_event_cb(btn_sort_artist, library_sort_artist_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_artist, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_artist = lv_label_create(btn_sort_artist);
    lv_label_set_text(lbl_sort_artist, "SORT ARTIST");
    lv_obj_set_style_text_font(lbl_sort_artist, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_artist, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_artist, LV_ALIGN_CENTER, 0, 0);

    // SORT NAME button
    lv_obj_t *btn_sort_name = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(btn_sort_name);
    lv_obj_add_style(btn_sort_name, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_name, 150, 45);
    lv_obj_set_pos(btn_sort_name, 630, 187);
    lv_obj_add_event_cb(btn_sort_name, library_sort_name_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_name, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_name = lv_label_create(btn_sort_name);
    lv_label_set_text(lbl_sort_name, "SORT NAME");
    lv_obj_set_style_text_font(lbl_sort_name, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_name, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_name, LV_ALIGN_CENTER, 0, 0);

    // SORT BPM button
    lv_obj_t *btn_sort_bpm = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(btn_sort_bpm);
    lv_obj_add_style(btn_sort_bpm, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_bpm, 150, 45);
    lv_obj_set_pos(btn_sort_bpm, 630, 242);
    lv_obj_add_event_cb(btn_sort_bpm, library_sort_bpm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_bpm, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_bpm = lv_label_create(btn_sort_bpm);
    lv_label_set_text(lbl_sort_bpm, "SORT BPM");
    lv_obj_set_style_text_font(lbl_sort_bpm, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_bpm, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_bpm, LV_ALIGN_CENTER, 0, 0);

    // Tip label
    s_label_library_hint = lv_label_create(s_screens[1]);
    lv_label_set_text(s_label_library_hint, "SELECT TRACK\nLOAD D1/D2");
    lv_obj_set_style_text_font(s_label_library_hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_library_hint, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(s_label_library_hint, 630, 300);
    ui_library_set_load_busy(false, NULL);
}

// Screen 3: HOT CUES Layout (2x4 Grid of pads)
static void create_screen_hot_cues(lv_obj_t *parent) {
    s_screens[2] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[2]);
    lv_obj_add_style(s_screens[2], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[2], UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(s_screens[2], 0, UI_CONTENT_Y);
    ui_create_performance_target_selector(s_screens[2], 298, 4);

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
        ui_format_time_cc(time_buf, sizeof(time_buf), s_hot_cue_positions[i]);
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
    ui_create_performance_target_selector(s_screens[3], 20, 8);

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
    ui_create_performance_target_selector(s_screens[4], 298, 0);

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
    ui_create_performance_target_selector(s_screens[5], 298, 4);

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

// ─── Waveform Helpers ────────────────────────────────────────────────────────

/* Render the high-resolution 1:1 overview waveform to the canvas once at track load. */
static void ui_load_waveform_data(uint8_t deck,
                                  uint32_t duration_ms,
                                  const uint8_t waveform_low[400],
                                  bool has_waveform,
                                  const anlz_metadata_t *meta)
{
    ui_cache_invalidate();
    uint8_t idx = ui_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    panel->last_fill_x = -1;
    panel->last_mini_fill_x = -1;
    panel->last_playhead_x = -1;
    if (!panel->wave_buf) return;

    uint8_t *buf = panel->wave_buf + 256 * sizeof(lv_color32_t);
    const int W = OVERVIEW_CV_W;
    const int H = OVERVIEW_CV_H;
    const int S = panel->wave_stride_px;

    // 1) Clear buffer to background index (0 == black)
    memset(buf, 0, (size_t)S * H * sizeof(uint8_t));

    // 2) Beat grid in the background (Pioneered-style vertical guides).
    if (meta && meta->beats && meta->beat_count > 0 && duration_ms > 0) {
        for (uint32_t b = 0; b < meta->beat_count; b++) {
            int32_t bt = (int32_t)meta->beats[b].time_ms;
            int x = (int)(((uint64_t)bt * W) / duration_ms);
            if (x < 0 || x >= W) continue;
            uint8_t col = (meta->beats[b].beat_phase == 0) ? 4 : 3;
            for (int y = 0; y < H; y++) {
                buf[y * S + x] = col;
            }
        }
    } else {
        for (int x = 40; x < W; x += 80) {
            for (int y = 0; y < H; y++) {
                buf[y * S + x] = 3;
            }
        }
        for (int y = 0; y < H; y++) {
            buf[y * S + (W / 2)] = 4;
        }
    }

    // 3) Waveform columns (bright blue index 1)
    if (has_waveform && waveform_low) {
        for (int x = 0; x < W; x++) {
            int src_x = (x * OVERVIEW_WAVEFORM_LOW_SAMPLES) / W;
            if (src_x >= OVERVIEW_WAVEFORM_LOW_SAMPLES) src_x = OVERVIEW_WAVEFORM_LOW_SAMPLES - 1;
            int amp = waveform_low[src_x] & 0x1F; // 0..31
            int h = (amp * (H - 4)) / 31;
            if (h < 1) h = 1;
            int cy = H / 2;
            int y0 = cy - h / 2;
            int y1 = cy + h / 2;
            if (y0 < 0) y0 = 0;
            if (y1 >= H) y1 = H - 1;
            for (int y = y0; y <= y1; y++) {
                buf[y * S + x] = 1; // 1 == upcoming waveform (premium blue)
            }
        }
    }

    if (meta && meta->beats && meta->beat_count > 0 && duration_ms > 0) {
        for (uint32_t b = 0; b < meta->beat_count; b++) {
            int32_t bt = (int32_t)meta->beats[b].time_ms;
            int x = (int)(((uint64_t)bt * W) / duration_ms);
            if (x < 0 || x >= W) continue;
            uint8_t col = (meta->beats[b].beat_phase == 0) ? 4 : 3;
            for (int y = 0; y < H; y++) {
                buf[y * S + x] = col;
            }
        }
    } else {
        for (int x = 40; x < W; x += 80) {
            for (int y = 0; y < H; y++) {
                buf[y * S + x] = 3;
            }
        }
        for (int y = 0; y < H; y++) {
            buf[y * S + (W / 2)] = 4;
        }
    }

    lv_obj_invalidate(panel->wave_canvas);

    if (panel->mini_wave_canvas && panel->mini_wave_buf) {
        uint8_t *mini_buf = panel->mini_wave_buf + 256 * sizeof(lv_color32_t);
        const int MW = OVERVIEW_MINI_CV_W;
        const int MH = OVERVIEW_MINI_CV_H;
        const int MS = panel->mini_wave_stride_px;
        memset(mini_buf, 0, (size_t)MS * MH * sizeof(uint8_t));

        if (has_waveform && waveform_low) {
            for (int x = 0; x < MW; x++) {
                int src_x = (x * OVERVIEW_WAVEFORM_LOW_SAMPLES) / MW;
                if (src_x >= OVERVIEW_WAVEFORM_LOW_SAMPLES) src_x = OVERVIEW_WAVEFORM_LOW_SAMPLES - 1;
                int amp = waveform_low[src_x] & 0x1F;
                int h = (amp * (MH - 2)) / 31;
                if (h < 1) h = 1;
                int cy = MH / 2;
                int y0 = cy - h / 2;
                int y1 = cy + h / 2;
                if (y0 < 0) y0 = 0;
                if (y1 >= MH) y1 = MH - 1;
                uint8_t color = (x % 17 < 3) ? 3 : 1;
                for (int y = y0; y <= y1; y++) {
                    mini_buf[y * MS + x] = color;
                }
            }
        }

        lv_obj_invalidate(panel->mini_wave_canvas);
    }
}

#ifdef WIN32
static void ui_load_waveform(const library_track_t *track)
{
    if (!track) return;
    ui_load_waveform_data(CTRL_DECK_1, track->duration_ms, track->waveform_low,
                          track->has_waveform != 0, ui_deck_anlz(CTRL_DECK_1));
}
#endif

#ifndef WIN32
static void ui_load_waveform_media(uint8_t deck, const media_loaded_track_t *track)
{
    if (!track) return;
    ui_load_waveform_data(deck, track->duration_ms, track->waveform_low,
                          track->has_waveform != 0, ui_deck_anlz(deck));
}
#endif

static void ui_update_overview_cue_markers(uint8_t deck)
{
    uint8_t deck_idx = ui_deck_index(deck);
    const anlz_metadata_t *meta = ui_deck_anlz(deck);
    uint32_t duration_ms = ui_deck_duration_ms(deck);
    static const uint32_t cue_hex_colors[8] = {
        0x00E676,
        0x00E5FF,
        0xFFAB00,
        0xE040FB,
        0xFFD600,
        0xFF1744,
        0x7C4DFF,
        0x2979FF
    };

    if (!meta || duration_ms == 0) {
        for (int i = 0; i < 8; i++) {
            if (s_overview_cue_markers[deck_idx][i]) {
                lv_obj_add_flag(s_overview_cue_markers[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    for (int i = 0; i < 8; i++) {
        lv_obj_t *marker = s_overview_cue_markers[deck_idx][i];
        if (!marker) {
            continue;
        }

        bool found = false;
        uint32_t pos = 0;
        for (int j = 0; j < meta->cue_count; j++) {
            if (meta->cues[j].index == i) {
                pos = meta->cues[j].start_ms;
                found = true;
                break;
            }
        }

        if (!found) {
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        float progress = (float)pos / (float)duration_ms;
        if (progress > 1.0f) progress = 1.0f;
        int marker_x = 10 + (int)(progress * (float)OVERVIEW_CV_W) - 1;
        lv_obj_set_pos(marker, marker_x, 7);
        lv_obj_set_style_bg_color(marker, lv_color_hex(cue_hex_colors[i]), LV_PART_MAIN);
        lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Update Hot Cue pads with real Rekordbox cue metadata */
static void ui_update_hot_cues(void)
{
    uint8_t deck = ui_performance_target_get(&s_performance_target);
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
            s_hot_cue_positions[i] = pos;
            s_hot_cue_ends[i] = end_pos;
            s_hot_cue_types[i] = type;
            
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
            s_hot_cue_positions[i] = 0xFFFFFFFF;
            s_hot_cue_ends[i] = 0;
            s_hot_cue_types[i] = 1;
            
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
            
            s_hot_cue_positions[i] = default_pos;
            s_hot_cue_ends[i] = 0;
            s_hot_cue_types[i] = 1;
            
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
// ── Firmware-only LVGL backend (PPA-rotated flush, tick, handler task) ───────

// LVGL rendered a full 800x480 landscape frame in `px_map`. Hardware-rotate it
// 90° (PPA angle 270°, matching the vendor demo) straight into the 480x800
// MIPI-DSI frame buffer, which the DPI engine scans out continuously.
static void ui_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    // Write back the rendered buffer so the PPA's DMA reads fresh pixels.
    size_t src_sz = (size_t)UI_HOR_RES * UI_VER_RES * 2;   // RGB565
    esp_cache_msync(px_map, src_sz, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    // Rotate into a back buffer (not the one currently on screen).
    void *target = s_dsi_fb[s_dsi_fb_idx];

    ppa_srm_oper_config_t op = {
        .in.buffer         = px_map,
        .in.pic_w          = UI_HOR_RES,
        .in.pic_h          = UI_VER_RES,
        .in.block_w        = UI_HOR_RES,
        .in.block_h        = UI_VER_RES,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,

        .out.buffer        = target,
        .out.buffer_size   = ALIGN_UP_BY((size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_align),
        .out.pic_w         = BSP_LCD_H_RES,   // 480 (rotated width)
        .out.pic_h         = BSP_LCD_V_RES,   // 800 (rotated height)
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm        = PPA_SRM_COLOR_MODE_RGB565,

        .rotation_angle    = PPA_SRM_ROTATION_ANGLE_270,
        .scale_x           = 1.0,
        .scale_y           = 1.0,
        .rgb_swap          = 0,
        .byte_swap         = 0,
        .mode              = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_do_scale_rotate_mirror(s_ppa, &op);   // blocking: target buffer ready on return

    // Switch the DPI to the freshly-rotated buffer. Because `target` is one of the
    // panel's own framebuffers, the driver just flips the active buffer (no copy)
    // and the hardware swaps at the next frame boundary → no tearing.
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, target);

    s_dsi_fb_idx = (s_dsi_fb_idx + 1) % UI_DSI_FB_COUNT;
    lv_display_flush_ready(disp);
}

static void ui_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// LVGL pointer read: poll the GT911 (coordinates already mapped to 800x480 by
// the BSP's swap_xy/mirror_x configuration).
static void ui_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    if (tp == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    esp_lcd_touch_point_data_t point = {0};
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(tp);
    esp_err_t rc = esp_lcd_touch_get_data(tp, &point, &cnt, 1);
    if (rc == ESP_OK && cnt > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void ui_lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL handler task started");
    while (1) {
        _lock_acquire_recursive(&s_lvgl_lock);
        uint32_t next_ms = lv_timer_handler();
        _lock_release_recursive(&s_lvgl_lock);
        if (next_ms > 100) next_ms = 100;   // cap to keep the UI responsive
        if (next_ms < 5)   next_ms = 5;     // avoid starving lower-prio tasks
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}

// Bring up LVGL on top of the BSP panel. Must run before any widget is created.
static esp_err_t ui_lvgl_backend_init(void)
{
    _lock_init_recursive(&s_lvgl_lock);

    esp_lcd_panel_handle_t panel = bsp_display_get_panel_handle();
    if (panel == NULL) {
        ESP_LOGE(TAG, "panel handle is NULL — call bsp_display_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    // Grab all 3 DPI framebuffers (480x800) for tear-free triple buffering.
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, UI_DSI_FB_COUNT,
                                                       &s_dsi_fb[0], &s_dsi_fb[1], &s_dsi_fb[2]));
    s_dsi_fb_idx = 1;   // fb[0] is the active buffer at boot; render into fb[1] first
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM, &s_cache_align));

    // Register the PPA Scale-Rotate-Mirror client used by the flush callback.
    ppa_client_config_t ppa_cfg = { .oper_type = PPA_OPERATION_SRM };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &s_ppa));

    lv_init();

    // Display is the 800x480 LANDSCAPE canvas the UI is built for; rotation to the
    // physical 480x800 panel happens in the flush callback via the PPA (LVGL's own
    // software rotation is not used).
    s_disp = lv_display_create(UI_HOR_RES, UI_VER_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(s_disp, panel);
    lv_display_set_flush_cb(s_disp, ui_lvgl_flush_cb);

    // Two full-screen render buffers (FULL mode → flush gets the whole frame,
    // so the PPA rotates one contiguous buffer per refresh). Cache-line aligned
    // in PSRAM so the PPA's DMA can read them directly.
    size_t buf_sz = ALIGN_UP_BY((size_t)UI_HOR_RES * UI_VER_RES * 2, s_cache_align);
    void *buf1 = heap_caps_aligned_alloc(s_cache_align, buf_sz, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_aligned_alloc(s_cache_align, buf_sz, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "failed to allocate %u-byte LVGL draw buffers from PSRAM", (unsigned)buf_sz);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_FULL);

    const esp_timer_create_args_t tick_args = {
        .callback = ui_lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Register the GT911 as an LVGL pointer device (if touch came up).
    esp_lcd_touch_handle_t tp = bsp_touch_get_handle();
    if (tp != NULL) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, tp);
        lv_indev_set_read_cb(indev, ui_touch_read_cb);
        ESP_LOGI(TAG, "GT911 registered as LVGL pointer input");
    } else {
        ESP_LOGW(TAG, "no touch handle — UI will be display-only");
    }

    ESP_LOGI(TAG, "LVGL backend ready (800x480 canvas → PPA-rotated to %dx%d, RGB565)",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

void ui_lvgl_lock(void)   { _lock_acquire_recursive(&s_lvgl_lock); }
void ui_lvgl_unlock(void) { _lock_release_recursive(&s_lvgl_lock); }
#else
void ui_lvgl_lock(void)   {}
void ui_lvgl_unlock(void) {}
#endif // !WIN32

esp_err_t ui_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL DJ UI layout (800x480 landscape)...");
    ui_deck_anlz_store_init(&s_deck_anlz_store);
    ui_performance_target_init(&s_performance_target);
    memset(s_perf_target_buttons, 0, sizeof(s_perf_target_buttons));
    s_perf_target_button_count = 0;
    memset(s_loop_active_by_deck, 0, sizeof(s_loop_active_by_deck));
    memset(s_loop_start_ms_by_deck, 0, sizeof(s_loop_start_ms_by_deck));
    memset(s_loop_end_ms_by_deck, 0, sizeof(s_loop_end_ms_by_deck));
    memset(s_loop_active_beats_by_deck, 0, sizeof(s_loop_active_beats_by_deck));

#ifndef WIN32
    // On firmware, bring up LVGL on top of the BSP panel before building widgets.
    // (On the PC simulator the HAL has already initialised LVGL + a display.)
    esp_err_t be_rc = ui_lvgl_backend_init();
    if (be_rc != ESP_OK) {
        return be_rc;
    }
#endif

    // Initialize custom dark themes
    init_styles();

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
    create_screen_overview(s_root_container);
    create_screen_library(s_root_container);
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

    // Load initial track metadata into the header displays
    mock_library_load_track_to_deck(0);
#ifdef WIN32
    library_track_t *track = library_get_ptr(0);
    if (track) {
        library_load_anlz(track);          // load BPM, duration, PWAV waveform
        
        /* Load detailed ANLZ metadata for the initial track */
        library_load_current_anlz(track);
        
        lv_label_set_text(s_label_title, track->title);
        lv_label_set_text(s_label_artist, track->artist);
        ui_label_set_f2(s_label_bpm, (float)track->bpm);
        ui_deck_track_info_set(CTRL_DECK_1, track->title, track->artist,
                               track->bpm, track->duration_ms);
        ui_deck_anlz_set_from_current(CTRL_DECK_1, library_get_current_anlz());
        ui_load_waveform(track);           // rebuild bar heights from PWAV data
        
        /* Populate hot cue points from active ANLZ metadata */
        ui_update_hot_cues();
    }
#else
    media_catalog_row_t row;
    media_loaded_track_t loaded;
    if (media_catalog_get_row(0, &row) == ESP_OK &&
        media_catalog_load(0, &loaded) == ESP_OK) {
        s_loaded_media[CTRL_DECK_1] = loaded;
        s_loaded_media_valid[CTRL_DECK_1] = true;
        s_loaded_media_source[CTRL_DECK_1] = loaded.source;
        ui_deck_track_info_set(CTRL_DECK_1,
                               row.title,
                               row.artist,
                               loaded.bpm ? loaded.bpm : row.bpm,
                               loaded.duration_ms);

        lv_label_set_text(s_label_title, row.title[0] ? row.title : "Unknown Title");
        lv_label_set_text(s_label_artist, row.artist[0] ? row.artist : "Unknown Artist");
        ui_label_set_f2(s_label_bpm, (float)(loaded.bpm ? loaded.bpm : row.bpm));
        ui_deck_anlz_set_from_current(CTRL_DECK_1, media_catalog_get_loaded_anlz_for_source(loaded.source));
        ui_load_waveform_media(CTRL_DECK_1, &loaded);
        ui_update_hot_cues();
    }
#endif

    // Register self-running LVGL timer to periodically refresh the UI states
    lv_timer_create(ui_timer_cb, 30, NULL);

#ifndef WIN32
    // Start the LVGL handler task last, once all widgets exist.
    if (xTaskCreate(ui_lvgl_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#endif

    ESP_LOGI(TAG, "LVGL DJ UI layout successfully initialized.");
    return ESP_OK;
}

void ui_trigger_library_refresh(void) {
    s_library_needs_refresh = true;
}

void ui_notify_usb_removed(void) {
#ifndef WIN32
    s_usb_removed_pending = true;
#endif
}

void ui_refresh_library(void) {
    if (!s_library_table) {
        return;
    }
    int n = ui_media_count();

#ifndef WIN32
    ESP_LOGI(TAG, "ui_refresh_library start. Free SRAM: %d B, SPIRAM: %d B",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif

    ui_lvgl_lock();
    ui_cache_invalidate();
    lv_table_set_row_count(s_library_table, n + 1);   // row 0 = header

    for (int i = 0; i < n; i++) {
        ui_fill_library_row(i);
    }

    s_selected_track_idx = 0;
#ifdef WIN32
    const library_track_t *track = library_get_ptr(0);
    if (track) {
        if (s_label_title)  lv_label_set_text(s_label_title, track->title);
        if (s_label_artist) lv_label_set_text(s_label_artist, track->artist);
        if (s_label_bpm)    ui_label_set_f2(s_label_bpm, (float)track->bpm);
    }
#else
    media_catalog_row_t row;
    if (media_catalog_get_row(0, &row) == ESP_OK) {
        if (s_label_title)  lv_label_set_text(s_label_title, row.title[0] ? row.title : "Unknown Title");
        if (s_label_artist) lv_label_set_text(s_label_artist, row.artist[0] ? row.artist : "Unknown Artist");
        if (s_label_bpm)    ui_label_set_f2(s_label_bpm, (float)row.bpm);
    }
#endif
    ui_update_library_source_label();
    ui_lvgl_unlock();

#ifndef WIN32
    ESP_LOGI(TAG, "ui_refresh_library end. Free SRAM: %d B, SPIRAM: %d B",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif

    ESP_LOGI(TAG, "library table refreshed: %d tracks", n);
}

static void ui_update_beat_indicator(const ui_beat_indicator_state_t *state)
{
    for (int i = 0; i < 4; i++) {
        if (!s_beat_pulses[i]) {
            continue;
        }

        bool active = state && state->valid && state->phase == (uint8_t)i;
        bool downbeat = active && state->downbeat;
        lv_opa_t opa = LV_OPA_40;
        if (active) {
            uint16_t progress = state->progress_permille > 1000 ? 1000 : state->progress_permille;
            opa = (lv_opa_t)(255u - ((uint32_t)progress * 135u) / 1000u);
        }

        ui_beat_dot_cache_t *cache = &s_cache_beat_dots[i];
        if (cache->valid &&
            cache->active == active &&
            cache->downbeat == downbeat &&
            cache->opa == opa) {
            continue;
        }
        cache->valid = true;
        cache->active = active;
        cache->downbeat = downbeat;
        cache->opa = opa;

        if (!active) {
            lv_obj_set_style_bg_color(s_beat_pulses[i], lv_color_hex(0x30343B), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_beat_pulses[i], LV_OPA_40, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_beat_pulses[i], lv_color_hex(0x4A515C), LV_PART_MAIN);
            continue;
        }

        // Beat-indicator colours stay inline (paired with the downbeat red, not chrome).
        lv_color_t color = downbeat ? lv_color_hex(0xFF1744) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_bg_color(s_beat_pulses[i], color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[i], opa, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[i], color, LV_PART_MAIN);
    }
}

static void ui_update_overview_waveform_progress(ui_overview_deck_panel_t *panel,
                                                 uint32_t position_ms,
                                                 uint32_t duration_ms)
{
    if (!panel || duration_ms == 0) return;

    float progress = (float)position_ms / (float)duration_ms;
    if (progress > 1.0f) progress = 1.0f;
    int playhead_x = (int)(progress * (float)OVERVIEW_CV_W);
    if (playhead_x < 0) playhead_x = 0;
    if (playhead_x > OVERVIEW_CV_W) playhead_x = OVERVIEW_CV_W;

    if (panel->playhead && playhead_x != panel->last_playhead_x) {
        lv_obj_set_pos(panel->playhead, 10 + playhead_x, 7);
        panel->last_playhead_x = playhead_x;
    }

    if (!panel->wave_canvas || !panel->wave_buf || playhead_x == panel->last_fill_x) {
        return;
    }

    uint8_t *buf = panel->wave_buf + 256 * sizeof(lv_color32_t);
    const int W = OVERVIEW_CV_W;
    const int H = OVERVIEW_CV_H;
    const int S = panel->wave_stride_px;
    int x0 = 0;
    int x1 = W;
    uint8_t target_wave_color = 1;

    if (panel->last_fill_x >= 0 && panel->last_fill_x <= W) {
        if (playhead_x > panel->last_fill_x) {
            x0 = panel->last_fill_x;
            x1 = playhead_x;
            target_wave_color = 2;
        } else {
            x0 = playhead_x;
            x1 = panel->last_fill_x;
            target_wave_color = 1;
        }
    }

    for (int x = x0; x < x1; x++) {
        uint8_t col = (panel->last_fill_x < 0) ? ((x < playhead_x) ? 2 : 1) : target_wave_color;
        for (int y = 0; y < H; y++) {
            uint8_t val = buf[y * S + x];
            if ((val == 1 || val == 2) && val != col) {
                buf[y * S + x] = col;
            }
        }
    }
    panel->last_fill_x = playhead_x;
    lv_obj_invalidate(panel->wave_canvas);

    if (!panel->mini_wave_canvas || !panel->mini_wave_buf) {
        return;
    }

    int mini_x = (int)(progress * (float)OVERVIEW_MINI_CV_W);
    if (mini_x < 0) mini_x = 0;
    if (mini_x > OVERVIEW_MINI_CV_W) mini_x = OVERVIEW_MINI_CV_W;
    if (panel->mini_playhead) {
        lv_obj_set_x(panel->mini_playhead, mini_x);
    }
    if (mini_x == panel->last_mini_fill_x) {
        return;
    }

    uint8_t *mini_buf = panel->mini_wave_buf + 256 * sizeof(lv_color32_t);
    const int MW = OVERVIEW_MINI_CV_W;
    const int MH = OVERVIEW_MINI_CV_H;
    const int MS = panel->mini_wave_stride_px;
    int mx0 = 0;
    int mx1 = MW;
    uint8_t mini_color = 1;
    if (panel->last_mini_fill_x >= 0 && panel->last_mini_fill_x <= MW) {
        if (mini_x > panel->last_mini_fill_x) {
            mx0 = panel->last_mini_fill_x;
            mx1 = mini_x;
            mini_color = 2;
        } else {
            mx0 = mini_x;
            mx1 = panel->last_mini_fill_x;
            mini_color = 1;
        }
    }

    for (int x = mx0; x < mx1; x++) {
        uint8_t col = (panel->last_mini_fill_x < 0) ? ((x < mini_x) ? 2 : 1) : mini_color;
        for (int y = 0; y < MH; y++) {
            uint8_t val = mini_buf[y * MS + x];
            if ((val == 1 || val == 2) && val != col) {
                mini_buf[y * MS + x] = col;
            }
        }
    }
    panel->last_mini_fill_x = mini_x;
    lv_obj_invalidate(panel->mini_wave_canvas);
}

#ifndef WIN32
static void ui_update_mixer_overview(const audio_engine_mixer_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        ui_overview_deck_panel_t *panel = &s_overview_decks[deck];
        ui_mixer_deck_view_t view =
            ui_mixer_deck_view_from_state(snapshot->channel_volume[deck],
                                          snapshot->output_gain[deck],
                                          snapshot->pfl_enabled[deck]);

        if (panel->label_ch) {
            lv_label_set_text_fmt(panel->label_ch, "CH %3u%%", (unsigned)view.fader_pct);
        }
        if (panel->label_out) {
            lv_label_set_text_fmt(panel->label_out, "OUT %3u%%", (unsigned)view.output_pct);
        }
        if (panel->label_pfl) {
            lv_label_set_text(panel->label_pfl, view.pfl_on ? "PFL ON" : "PFL OFF");
            lv_obj_set_style_text_color(panel->label_pfl,
                                        view.pfl_on ? COL_AMBER : COL_TEXT_DIM,
                                        LV_PART_MAIN);
        }
        if (panel->out_bar_fill) {
            int w = (78 * (int)view.output_pct) / 100;
            if (w < 2) w = 2;
            lv_obj_set_width(panel->out_bar_fill, w);
            lv_obj_set_style_bg_color(panel->out_bar_fill,
                                      deck == CTRL_DECK_1 ? COL_ACCENT : COL_GREEN,
                                      LV_PART_MAIN);
        }
    }

    if (s_crossfader_knob) {
        int knob_x = ui_mixer_crossfader_knob_x(snapshot->crossfader, s_crossfader_track_w);
        lv_obj_set_x(s_crossfader_knob, OVERVIEW_XFADER_X + knob_x - 3);
    }
}
#endif

static void ui_update_overview_deck(uint8_t deck, const deck_state_t *state)
{
    uint8_t idx = ui_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    if (!panel->panel || !state) return;

    uint32_t duration_ms = ui_deck_duration_ms(idx);
    uint32_t elapsed_ms = state->position_ms;
    uint32_t remain_ms = (duration_ms > elapsed_ms) ? (duration_ms - elapsed_ms) : 0;
    const ui_deck_track_info_t *info = &s_deck_track_info[idx];

    lv_label_set_text(panel->label_status, state->playing ? "((PLAY))" : (info->valid ? "LOADED" : "EMPTY"));
    lv_obj_set_style_text_color(panel->label_status,
                                state->playing ? COL_RED : (info->valid ? COL_AMBER : COL_TEXT_DIM),
                                LV_PART_MAIN);
    lv_label_set_text(panel->label_title, info->valid ? info->title : "No Track");
    lv_label_set_text(panel->label_artist, info->valid ? info->artist : "");

    lv_label_set_text_fmt(panel->label_time, "%02u:%02u.%02u",
                          (unsigned)(elapsed_ms / 60000),
                          (unsigned)((elapsed_ms % 60000) / 1000),
                          (unsigned)((elapsed_ms % 1000) / 10));
    lv_label_set_text_fmt(panel->label_remain, "-%02u:%02u.%02u",
                          (unsigned)(remain_ms / 60000),
                          (unsigned)((remain_ms % 60000) / 1000),
                          (unsigned)((remain_ms % 1000) / 10));

    float pitch_pct;
#ifndef WIN32
    pitch_pct = audio_engine_raw_pitch_to_percent(state->pitch);
#else
    pitch_pct = ((8192.0f - (float)state->pitch) / 8192.0f) * 10.0f;
#endif
    uint16_t base_bpm = ui_deck_bpm(idx);
    float current_bpm = (float)(base_bpm ? base_bpm : 120) * (1.0f + (pitch_pct / 100.0f));
    ui_label_set_f2(panel->label_bpm, current_bpm);
    int pc = (int)(pitch_pct * 100.0f + (pitch_pct >= 0.0f ? 0.5f : -0.5f));
    lv_label_set_text_fmt(panel->label_pitch, "%c%d.%02d%%",
                          (pc < 0) ? '-' : '+',
                          (pc < 0 ? -pc : pc) / 100,
                          (pc < 0 ? -pc : pc) % 100);

    ui_update_overview_waveform_progress(panel, elapsed_ms, duration_ms);
}

void ui_update(void) {
#ifndef WIN32
    if (s_usb_removed_pending) {
        ui_apply_usb_removed();
    }
    ui_poll_track_load_result();
#endif

    if (s_library_needs_refresh) {
        s_library_needs_refresh = false;
        ui_refresh_library();
    }

    // Keep the library table constantly focused while on the LIBRARY tab to preserve the neon blue selection highlight
    if (s_active_tab == 1 && s_library_table) {
        lv_group_t *g = lv_group_get_default();
        if (g && lv_group_get_focused(g) != s_library_table) {
            lv_group_focus_obj(s_library_table);
        }
        
        // Ensure the active track is explicitly and persistently highlighted in blue
        uint32_t sel_row = LV_TABLE_CELL_NONE;
        uint32_t sel_col = LV_TABLE_CELL_NONE;
        lv_table_get_selected_cell(s_library_table, &sel_row, &sel_col);
        
        // Only restore the selection if there is currently NO selection active (or if it's on the header row 0)
        if (sel_row == LV_TABLE_CELL_NONE || sel_row == 0) {
            lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
        }
    }

    // Read state snapshot
    deck_state_t state = deck_core_get_state();
    deck_state_t deck2_state = deck_core_get_deck_state(CTRL_DECK_2);
    uint8_t active_deck = ui_performance_target_get(&s_performance_target);
    deck_state_t active_state = active_deck == CTRL_DECK_1 ? state : deck2_state;
    uint32_t active_duration_ms = ui_deck_duration_ms(active_deck);
    uint16_t active_base_bpm = ui_deck_bpm(active_deck);
    const anlz_metadata_t *active_meta = ui_deck_anlz(active_deck);
    ui_update_active_header_track(active_deck);
    
    // Pre-calculate beat indicator state for LEDs and UI
    ui_beat_indicator_state_t beat_state = {0};
    bool beat_state_valid = false;
    if (active_duration_ms > 0) {
        beat_state = ui_beat_indicator_calculate(active_state.position_ms,
                                                 active_meta ? active_meta->beats : NULL,
                                                 active_meta ? active_meta->beat_count : 0,
                                                 active_base_bpm);
        beat_state_valid = beat_state.valid;
    }

    // P5a: preload feedback (firmware only)
#ifndef WIN32
    bool    ae_loading  = (audio_engine_get_state() == AE_LOADING);
    uint8_t ae_load_pct = audio_engine_load_progress();
    audio_engine_get_mixer_snapshot(&s_cache_mixer_snapshot);
#else
    bool    ae_loading  = false;
    uint8_t ae_load_pct = 100;
    (void)ae_load_pct;
#endif

    // ─── 1. Simulate Loop constraints inside update loop (for simulator) ───
#ifdef WIN32
    if (s_loop_active) {
        if (state.position_ms >= s_loop_end_ms) {
            mock_deck_set_position(s_loop_start_ms);
            state.position_ms = s_loop_start_ms;
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
        uint32_t elapsed_centis = elapsed_ms / 10u;
        uint32_t remain_centis = remain_ms / 10u;
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
    if (s_active_tab == 0 && active_duration_ms > 0) {
        ui_update_beat_indicator(beat_state_valid ? &beat_state : NULL);
        #ifndef WIN32
        ui_update_mixer_overview(&s_cache_mixer_snapshot);
        #endif
        ui_update_overview_deck(CTRL_DECK_1, &state);
        ui_update_overview_deck(CTRL_DECK_2, &deck2_state);
        ui_update_overview_cue_markers(CTRL_DECK_1);
        ui_update_overview_cue_markers(CTRL_DECK_2);
    } else if (s_active_tab == 0) {
        ui_update_beat_indicator(NULL);
        #ifndef WIN32
        ui_update_mixer_overview(&s_cache_mixer_snapshot);
        #endif
        ui_update_overview_deck(CTRL_DECK_1, &state);
        ui_update_overview_deck(CTRL_DECK_2, &deck2_state);
        ui_update_overview_cue_markers(CTRL_DECK_1);
        ui_update_overview_cue_markers(CTRL_DECK_2);
    }

    // ─── 6. Sync UI Status bar with mock settings ───
    if (s_active_tab == 6) {
#ifndef WIN32
        ui_update_uart_status_label(&state);
        ui_update_link_status_label();
        ui_update_sd_status_label(false);
        ui_update_sd_cache_status_label(false);
#endif
    }
}
