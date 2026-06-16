#include "ui_overview.h"

#include "lvgl.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "deck_core.h"
#include "ui_beat_indicator.h"
#include "ui_diagnostics.h"
#include "ui_lvgl_backend.h"
#include "ui_mixer_view.h"
#include "ui_overview_motion.h"
#include "ui_overlay_map.h"
#include "ui_overview_perf.h"
#include "ui_overview_renderer.h"
#include "ui_overview_scheduler.h"
#include "ui_overview_wave_cache.h"
#include "ui_position_interpolator.h"
#include "ui_waveform_model.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32
#include "audio_engine.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

#ifndef UI_HOR_RES
#define UI_HOR_RES   800
#define UI_VER_RES   480
#define UI_TOPBAR_H   46
#define UI_CONTENT_Y  UI_TOPBAR_H
#define UI_CONTENT_H  (UI_VER_RES - UI_TOPBAR_H)
#endif

static const char *TAG = "ui_overview";
static ui_overview_config_t s_overview_config;

#define s_style_screen_bg  (*s_overview_config.styles.screen_bg)
#define s_style_panel_frame (*s_overview_config.styles.panel_frame)
#define s_style_btn_primary (*s_overview_config.styles.btn_primary)
#define s_style_btn_amber (*s_overview_config.styles.btn_amber)
#define s_style_pressed (*s_overview_config.styles.pressed)

static uint8_t ui_overview_deck_index(uint8_t deck)
{
    return deck < DECK_CORE_DECK_COUNT ? deck : DECK_CORE_COMPAT_DECK;
}

static uint8_t ui_event_deck(lv_event_t *e)
{
    if (!e) return CTRL_DECK_1;
    lv_obj_t *target = lv_event_get_target(e);
    return ui_overview_deck_index((uint8_t)(uintptr_t)lv_obj_get_user_data(target));
}

static void overview_deck_select_event_cb(lv_event_t *e)
{
    uint8_t deck = ui_event_deck(e);
    if (s_overview_config.actions.select_deck) {
        s_overview_config.actions.select_deck(deck);
    }
}

static void play_pause_event_cb(lv_event_t *e)
{
    uint8_t deck = ui_event_deck(e);
    if (s_overview_config.actions.play_pause) {
        s_overview_config.actions.play_pause(deck);
    }
}

static void cue_event_cb(lv_event_t *e)
{
    uint8_t deck = ui_event_deck(e);
    if (s_overview_config.actions.cue) {
        s_overview_config.actions.cue(deck);
    }
}

static bool ui_label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label) return false;
    const char *safe_text = text ? text : "";
    const char *current = lv_label_get_text(label);
    if (current && strcmp(current, safe_text) == 0) return false;
    lv_label_set_text(label, safe_text);
    return true;
}

static void ui_obj_set_text_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
    if (!obj) return;
    if (lv_color_to_u32(lv_obj_get_style_text_color(obj, LV_PART_MAIN)) == lv_color_to_u32(color)) return;
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
}

static void ui_obj_set_bg_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
    if (!obj) return;
    if (lv_color_to_u32(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)) == lv_color_to_u32(color)) return;
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
}

static void ui_obj_set_bg_opa_if_changed(lv_obj_t *obj, lv_opa_t opa)
{
    if (!obj || lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == opa) return;
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
}

static void ui_obj_set_x_if_changed(lv_obj_t *obj, int32_t x)
{
    if (!obj || lv_obj_get_x(obj) == x) return;
    lv_obj_set_x(obj, x);
}

// Waveform visualizer definitions
#define OVERVIEW_CV_W 648
#define OVERVIEW_CV_H 141
#define OVERVIEW_WAVE_STRIP_MARGIN_PX UI_OVERVIEW_WAVE_CACHE_MARGIN_PX
#define OVERVIEW_WAVE_STRIP_W (OVERVIEW_CV_W + (OVERVIEW_WAVE_STRIP_MARGIN_PX * 2))
_Static_assert(OVERVIEW_WAVE_STRIP_W > OVERVIEW_CV_W, "wave strip must be wider than visible canvas");
#define OVERVIEW_MINI_CV_W 392
#define OVERVIEW_MINI_CV_H 45
#define OVERVIEW_WAVE_X 82
#define OVERVIEW_WAVE_INSET_X 0
#define OVERVIEW_WAVE_INSET_Y 0
#define OVERVIEW_DECK1_WAVE_Y 0
#define OVERVIEW_DECK2_WAVE_Y 142
#define OVERVIEW_WAVE_CENTER_X (OVERVIEW_WAVE_X + OVERVIEW_WAVE_INSET_X + (OVERVIEW_CV_W / 2))
#define OVERVIEW_BEAT_PULSES_Y 144
#define OVERVIEW_BEAT_PULSES_X (OVERVIEW_WAVE_CENTER_X - 33)
#define OVERVIEW_MAIN_VISIBLE_BEATS 16u
#define OVERVIEW_MAIN_MIN_WINDOW_MS 4000u
#define OVERVIEW_MAIN_MAX_WINDOW_MS 30000u
#define OVERVIEW_PHASE_W 240
#define OVERVIEW_PHASE_X (OVERVIEW_WAVE_CENTER_X - (OVERVIEW_PHASE_W / 2))
#define OVERVIEW_PHASE_Y 150
#define OVERVIEW_PHASE_KNOB_W 8
#define OVERVIEW_PLAYHEAD_W 3
#define OVERVIEW_OUTLINE_W 1
#define OVERVIEW_DECK_INFO_W 400
#define OVERVIEW_TITLE_Y 312
#define OVERVIEW_TITLE_H 30
#define OVERVIEW_TITLE_TEXT_W 224
#define OVERVIEW_TITLE_TIMER_X 248
#define OVERVIEW_TITLE_TIMER_Y 314
#define OVERVIEW_TITLE_TIMER_MAIN_W 90
#define OVERVIEW_TITLE_TIMER_FRACTION_X (OVERVIEW_TITLE_TIMER_X + OVERVIEW_TITLE_TIMER_MAIN_W + 2)
#define OVERVIEW_TITLE_TIMER_FRACTION_W 46
#define OVERVIEW_INFO_DIVIDER_Y 344
#define OVERVIEW_INFO_ROW_Y 346
#define OVERVIEW_TIME_Y 354
#define OVERVIEW_MIX_ROW_Y 370
#define OVERVIEW_BPM_X 184
#define OVERVIEW_BPM_Y 348
#define OVERVIEW_BPM_W 58
#define OVERVIEW_BPM_TAG_X 246
#define OVERVIEW_PITCH_X 282
#define OVERVIEW_PITCH_Y 350
#define OVERVIEW_PITCH_W 108
#define OVERVIEW_MINI_WAVE_Y 386
#define OVERVIEW_TIME_UPDATE_MS 50u
#define OVERVIEW_SIDE_BTN_H 38

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *label_deck;
    lv_obj_t *label_status;
    lv_obj_t *label_title;
    lv_obj_t *label_artist;
    lv_obj_t *label_time;
    lv_obj_t *label_time_secs;
    lv_obj_t *label_time_fraction;
    lv_obj_t *label_remain;
    lv_obj_t *label_bpm;
    lv_obj_t *label_pitch;
    lv_obj_t *label_ch;
    lv_obj_t *label_out;
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
    int       last_mini_fill_x;
    int       last_playhead_x;
    uint32_t  last_wave_center_ms;
    uint32_t  last_wave_window_ms;
    uint32_t  last_time_bucket;
    uint32_t  last_remain_bucket;
} ui_overview_deck_panel_t;

static ui_overview_deck_panel_t s_overview_decks[DECK_CORE_DECK_COUNT];
static bool s_overview_deck_pfl[DECK_CORE_DECK_COUNT];
static bool s_overview_deck_playing[DECK_CORE_DECK_COUNT];
static lv_obj_t *s_beat_pulses[DECK_CORE_DECK_COUNT][4];
static lv_obj_t *s_overview_cue_heads[DECK_CORE_DECK_COUNT][8];
static lv_obj_t *s_overview_fx_panel = NULL;
static ui_overview_perf_counter_t s_overview_wave_perf[DECK_CORE_DECK_COUNT];
static ui_position_interpolator_t s_overview_position_interp[DECK_CORE_DECK_COUNT];
static ui_overview_scheduler_t s_overview_scheduler;
#ifndef WIN32
#define UI_RGB565(r, g, b) \
    (uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | (((uint16_t)(g) & 0xFCu) << 3) | ((uint16_t)(b) >> 3))
static const uint16_t s_overview_wave_rgb565_palette[] = {
    UI_RGB565(0x00, 0x00, 0x00),
    UI_RGB565(0xF0, 0x2B, 0x72),
    UI_RGB565(0x26, 0x65, 0xFF),
    UI_RGB565(0x1D, 0x5F, 0x5E),
    UI_RGB565(0xE5, 0xE6, 0xEA),
    UI_RGB565(0x1D, 0xF5, 0x94),
    UI_RGB565(0xFF, 0xB3, 0x38),
    UI_RGB565(0x9B, 0x5C, 0xFF),
    UI_RGB565(0x5A, 0x5D, 0x64),
};
static uint16_t *s_overview_wave_overlay_rgb565[DECK_CORE_DECK_COUNT] = { NULL };
static size_t    s_overview_wave_overlay_bytes = 0;
static ui_overview_wave_cache_t s_overview_wave_cache[DECK_CORE_DECK_COUNT];
static ui_overview_perf_counter_t s_overview_overlay_total_perf[DECK_CORE_DECK_COUNT];
static ui_overview_perf_counter_t s_overview_overlay_msync_perf[DECK_CORE_DECK_COUNT];
static ui_overview_perf_counter_t s_overview_overlay_ppa_perf[DECK_CORE_DECK_COUNT];
#endif
static lv_obj_t *s_phase_meter_label = NULL;
#if 0
static lv_obj_t *s_phase_meter_track = NULL;
static lv_obj_t *s_phase_meter_center = NULL;
#endif
static lv_obj_t *s_phase_meter_knob = NULL;
static uint32_t ui_overview_main_window_ms(uint8_t deck, const anlz_metadata_t *meta);

static lv_obj_t *s_overview_cue_markers[DECK_CORE_DECK_COUNT][8];
static uint32_t s_overview_deck_duration_ms[DECK_CORE_DECK_COUNT];
static uint16_t s_overview_deck_bpm[DECK_CORE_DECK_COUNT];
static const anlz_metadata_t *s_overview_deck_meta[DECK_CORE_DECK_COUNT];
static const ui_deck_track_info_t *s_overview_deck_info[DECK_CORE_DECK_COUNT];
static ui_overview_waveform_source_info_t s_overview_wave_source[DECK_CORE_DECK_COUNT];
static int s_overview_active_tab = 0;

static void ui_overview_invalidate_mini_wave_range(const ui_overview_deck_panel_t *panel,
                                                   int x0,
                                                   int x1)
{
    if (!panel || !panel->mini_wave_canvas || x0 >= x1) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (x1 > OVERVIEW_MINI_CV_W) x1 = OVERVIEW_MINI_CV_W;
    if (x0 >= x1) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(panel->mini_wave_canvas, &coords);
    lv_area_t area = {
        .x1 = coords.x1 + x0,
        .y1 = coords.y1,
        .x2 = coords.x1 + x1 - 1,
        .y2 = coords.y2,
    };
    lv_obj_invalidate_area(panel->mini_wave_canvas, &area);
}

// Screen 1: OVERVIEW Layout
// Tap-to-seek on the overview waveform: jump playback to the tapped position
// inside the visible zoom window.
static void waveform_seek_event_cb(lv_event_t *e) {
    lv_obj_t *wv = lv_event_get_target(e);
    uint8_t deck = ui_event_deck(e);
    uint8_t idx = ui_overview_deck_index(deck);
    uint32_t duration_ms = s_overview_deck_duration_ms[idx];
    if (duration_ms == 0) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t content;
    lv_obj_get_content_coords(wv, &content);
    int rel_x = (int)p.x - content.x1 - OVERVIEW_WAVE_INSET_X;
    if (rel_x < 0)   rel_x = 0;
    if (rel_x > OVERVIEW_CV_W) rel_x = OVERVIEW_CV_W;

    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    const anlz_metadata_t *meta = s_overview_deck_meta[idx];
    uint32_t window_ms = panel->last_wave_window_ms > 0
                       ? panel->last_wave_window_ms
                       : ui_overview_main_window_ms(deck, meta);
    uint32_t center_ms = panel->last_wave_center_ms != UINT32_MAX
                       ? panel->last_wave_center_ms
                       : 0;
    int64_t window_start_ms = (int64_t)center_ms - ((int64_t)window_ms / 2);
    int64_t target = window_start_ms +
        (((int64_t)rel_x * (int64_t)window_ms) / OVERVIEW_CV_W);
    if (target < 0) target = 0;
    if (target > (int64_t)duration_ms) target = duration_ms;
    uint32_t target_ms = (uint32_t)target;

    if (s_overview_config.actions.seek) {
        s_overview_config.actions.seek(deck, target_ms);
    }
    ESP_LOGI(TAG, "D%u waveform seek -> %lu ms (zoom x=%d%%)",
             (unsigned)deck + 1u, (unsigned long)target_ms,
             (int)((rel_x * 100) / OVERVIEW_CV_W));
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

static void ui_overview_apply_deck_badge(uint8_t deck)
{
    uint8_t idx = ui_overview_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    if (!panel->label_deck) {
        return;
    }

    bool pfl_on = s_overview_deck_pfl[idx];
    bool playing = s_overview_deck_playing[idx];
    lv_color_t bg = pfl_on ? COL_GREEN : COL_RED;
    lv_color_t text = pfl_on ? COL_ON_ACCENT : COL_TEXT;

    lv_obj_set_style_bg_color(panel->label_deck, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_deck, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(panel->label_deck, text, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel->label_deck, playing ? COL_TEXT : bg, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->label_deck, playing ? 3 : 1, LV_PART_MAIN);
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
    lv_obj_set_size(btn, w, OVERVIEW_SIDE_BTN_H);
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

static bool ui_overview_main_wave_ready(const ui_overview_deck_panel_t *panel)
{
#ifndef WIN32
    return panel && panel->wave_border;
#else
    return panel && panel->wave_canvas && panel->wave_buf;
#endif
}

static void cue_head_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_color_t color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.color = color;
    tri_dsc.opa = LV_OPA_COVER;

    /* Triangle pointing down: top-left, top-right, bottom-center */
    tri_dsc.p[0].x = coords.x1;
    tri_dsc.p[0].y = coords.y1;

    tri_dsc.p[1].x = coords.x2;
    tri_dsc.p[1].y = coords.y1;

    tri_dsc.p[2].x = (coords.x1 + coords.x2) / 2;
    tri_dsc.p[2].y = coords.y2;

    lv_draw_triangle(layer, &tri_dsc);
}

static void ui_create_overview_deck_panel(lv_obj_t *parent, uint8_t deck, int y)
{
    (void)y;
    ui_overview_deck_panel_t *panel = &s_overview_decks[ui_overview_deck_index(deck)];
    panel->wave_stride_px = OVERVIEW_CV_W;
    panel->mini_wave_stride_px = OVERVIEW_MINI_CV_W;
    panel->last_mini_fill_x = -1;
    panel->last_playhead_x = -1;
    panel->last_wave_center_ms = UINT32_MAX;
    panel->last_wave_window_ms = 0;
    panel->last_time_bucket = UINT32_MAX;
    panel->last_remain_bucket = UINT32_MAX;
    int top_y = (deck == CTRL_DECK_1) ? 0 : 158;
    int wave_y = (deck == CTRL_DECK_1) ? OVERVIEW_DECK1_WAVE_Y : OVERVIEW_DECK2_WAVE_Y;
    int info_x = (deck == CTRL_DECK_1) ? 0 : 400;

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

    panel->label_deck = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                COL_TEXT, 0, top_y + 4, 88,
                                                deck == CTRL_DECK_1 ? "DECK 1" : "DECK 2");
    lv_obj_set_size(panel->label_deck, 88, 52);
    lv_obj_set_style_bg_color(panel->label_deck, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_deck, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel->label_deck, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel->label_deck, 10, LV_PART_MAIN);
    lv_obj_set_user_data(panel->label_deck, (void *)(uintptr_t)deck);
    lv_obj_add_flag(panel->label_deck, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel->label_deck, overview_deck_select_event_cb, LV_EVENT_CLICKED, NULL);
    ui_overview_apply_deck_badge(deck);
    panel->label_status = NULL;
    ui_overview_bar(panel->panel, info_x, OVERVIEW_TITLE_Y,
                    OVERVIEW_DECK_INFO_W, OVERVIEW_TITLE_H, COL_TITLE_BLUE);
    panel->label_title = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                 COL_TEXT, info_x, OVERVIEW_TITLE_Y,
                                                 OVERVIEW_TITLE_TEXT_W, "No Track");
    lv_label_set_long_mode(panel->label_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(panel->label_title, OVERVIEW_TITLE_H);
    lv_obj_set_style_bg_color(panel->label_title, COL_TITLE_BLUE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_title, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel->label_title, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel->label_title, 0, LV_PART_MAIN);
    panel->label_artist = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                                  COL_TEXT_MUTED, info_x + 8, OVERVIEW_INFO_ROW_Y, 118, "TRACK");
    lv_obj_add_flag(panel->label_artist, LV_OBJ_FLAG_HIDDEN);
    panel->label_time = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                COL_TEXT, info_x + OVERVIEW_TITLE_TIMER_X,
                                                OVERVIEW_TITLE_TIMER_Y,
                                                48, "-00");
    lv_obj_set_height(panel->label_time, OVERVIEW_TITLE_H);
    lv_obj_set_style_text_align(panel->label_time, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    lv_obj_t *label_colon = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                    COL_TEXT, info_x + OVERVIEW_TITLE_TIMER_X + 48,
                                                    OVERVIEW_TITLE_TIMER_Y,
                                                    8, ":");
    lv_obj_set_height(label_colon, OVERVIEW_TITLE_H);
    lv_obj_set_style_text_align(label_colon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    panel->label_time_secs = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                     COL_TEXT, info_x + OVERVIEW_TITLE_TIMER_X + 56,
                                                     OVERVIEW_TITLE_TIMER_Y,
                                                     34, "00");
    lv_obj_set_height(panel->label_time_secs, OVERVIEW_TITLE_H);
    lv_obj_set_style_text_align(panel->label_time_secs, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    panel->label_time_fraction =
        ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                COL_TEXT, info_x + OVERVIEW_TITLE_TIMER_FRACTION_X,
                                OVERVIEW_TITLE_TIMER_Y,
                                OVERVIEW_TITLE_TIMER_FRACTION_W, ".00");
    lv_obj_set_height(panel->label_time_fraction, OVERVIEW_TITLE_H);
    lv_obj_set_style_text_align(panel->label_time_fraction, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    panel->label_remain = ui_overview_value_label(panel->panel, &lv_font_montserrat_16,
                                                  COL_TEXT_MUTED, info_x + 254, OVERVIEW_TIME_Y + 8, 68, "-00:00");
    lv_obj_add_flag(panel->label_remain, LV_OBJ_FLAG_HIDDEN);
    panel->label_bpm = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                               COL_TEXT, info_x + OVERVIEW_BPM_X,
                                               OVERVIEW_BPM_Y, OVERVIEW_BPM_W, "120");
    lv_obj_set_style_text_align(panel->label_bpm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    panel->label_pitch = ui_overview_value_label(panel->panel, &lv_font_montserrat_16,
                                                 COL_TEXT_MUTED, info_x + OVERVIEW_PITCH_X,
                                                 OVERVIEW_PITCH_Y, OVERVIEW_PITCH_W, "+0.00%");

    ui_overview_bar(panel->panel, info_x, OVERVIEW_INFO_DIVIDER_Y, OVERVIEW_DECK_INFO_W, 1, COL_BORDER);
    ui_overview_value_label(panel->panel, &lv_font_montserrat_12, COL_TEXT_MUTED,
                            info_x + OVERVIEW_BPM_TAG_X, OVERVIEW_BPM_Y + 8, 30, "BPM");
    panel->label_ch = NULL;
    panel->label_out = NULL;
    panel->out_bar_bg = NULL;
    panel->out_bar_fill = NULL;

    panel->wave_border = lv_obj_create(panel->panel);
    lv_obj_remove_style_all(panel->wave_border);
    lv_obj_add_style(panel->wave_border, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->wave_border, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel->wave_border, lv_color_hex(0x020406), LV_PART_MAIN);
    lv_obj_set_size(panel->wave_border, OVERVIEW_CV_W + (OVERVIEW_WAVE_INSET_X * 2),
                    OVERVIEW_CV_H + (OVERVIEW_WAVE_INSET_Y * 2));
    lv_obj_set_pos(panel->wave_border, OVERVIEW_WAVE_X, wave_y);
    lv_obj_set_style_pad_all(panel->wave_border, 0, LV_PART_MAIN);
    lv_obj_set_user_data(panel->wave_border, (void *)(uintptr_t)deck);
    lv_obj_remove_flag(panel->wave_border, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel->wave_border, waveform_seek_event_cb, LV_EVENT_CLICKED, NULL);

#ifdef WIN32
    size_t ov_sz = LV_DRAW_BUF_SIZE(OVERVIEW_CV_W, OVERVIEW_CV_H, LV_COLOR_FORMAT_I8);
    panel->wave_buf = malloc(ov_sz);
    if (panel->wave_buf) {
        memset(panel->wave_buf, 0, ov_sz);
        panel->wave_canvas = lv_canvas_create(panel->wave_border);
        lv_canvas_set_buffer(panel->wave_canvas, panel->wave_buf, OVERVIEW_CV_W, OVERVIEW_CV_H, LV_COLOR_FORMAT_I8);
        lv_obj_align(panel->wave_canvas, LV_ALIGN_TOP_LEFT, OVERVIEW_WAVE_INSET_X, OVERVIEW_WAVE_INSET_Y);
        lv_obj_remove_flag(panel->wave_canvas, LV_OBJ_FLAG_CLICKABLE);

        lv_canvas_set_palette(panel->wave_canvas, 0, lv_color32_make(0x00, 0x00, 0x00, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 1, lv_color32_make(0xF0, 0x2B, 0x72, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 2, lv_color32_make(0x26, 0x65, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 3, lv_color32_make(0x1D, 0x5F, 0x5E, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 4, lv_color32_make(0xE5, 0xE6, 0xEA, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 5, lv_color32_make(0x1D, 0xF5, 0x94, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 6, lv_color32_make(0xFF, 0xB3, 0x38, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 7, lv_color32_make(0x9B, 0x5C, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 8, lv_color32_make(0x5A, 0x5D, 0x64, 0xFF));

        lv_image_dsc_t *dsc = lv_canvas_get_image(panel->wave_canvas);
        if (dsc && dsc->header.stride > 0) panel->wave_stride_px = (int)dsc->header.stride;
    } else {
        ESP_LOGE(TAG, "D%u overview canvas buffer alloc failed (%u bytes)",
                 (unsigned)deck + 1u, (unsigned)ov_sz);
    }
#endif

    panel->playhead = lv_obj_create(panel->wave_border);
    lv_obj_set_style_bg_color(panel->playhead, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->playhead, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->playhead, OVERVIEW_PLAYHEAD_W, OVERVIEW_CV_H);
    lv_obj_set_pos(panel->playhead,
                   OVERVIEW_WAVE_INSET_X + (OVERVIEW_CV_W / 2) - (OVERVIEW_PLAYHEAD_W / 2),
                   OVERVIEW_WAVE_INSET_Y);
    lv_obj_remove_flag(panel->playhead, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(panel->wave_border);

    panel->mini_wave_border = lv_obj_create(panel->panel);
    lv_obj_remove_style_all(panel->mini_wave_border);
    lv_obj_set_style_bg_color(panel->mini_wave_border, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->mini_wave_border, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->mini_wave_border, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->mini_wave_border, OVERVIEW_MINI_CV_W, OVERVIEW_MINI_CV_H);
    lv_obj_set_pos(panel->mini_wave_border, info_x + 4, OVERVIEW_MINI_WAVE_Y);
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
        lv_canvas_set_palette(panel->mini_wave_canvas, 2, lv_color32_make(0x26, 0x65, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 3, lv_color32_make(0x1D, 0x5F, 0x5E, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 4, lv_color32_make(0xE5, 0xE6, 0xEA, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 5, lv_color32_make(0x1D, 0xF5, 0x94, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 6, lv_color32_make(0xFF, 0xB3, 0x38, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 7, lv_color32_make(0x9B, 0x5C, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 8, lv_color32_make(0x5A, 0x5D, 0x64, 0xFF));
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

    uint8_t deck_idx = ui_overview_deck_index(deck);
    for (int i = 0; i < 8; i++) {
        s_overview_cue_markers[deck_idx][i] = lv_obj_create(panel->wave_border);
        lv_obj_set_style_border_width(s_overview_cue_markers[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_size(s_overview_cue_markers[deck_idx][i], 2, OVERVIEW_CV_H - 4);
        lv_obj_add_flag(s_overview_cue_markers[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_overview_cue_markers[deck_idx][i], LV_OBJ_FLAG_CLICKABLE);

        s_overview_cue_heads[deck_idx][i] = lv_obj_create(panel->wave_border);
        lv_obj_set_style_border_width(s_overview_cue_heads[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_overview_cue_heads[deck_idx][i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_size(s_overview_cue_heads[deck_idx][i], 7, 7);
        lv_obj_add_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_overview_cue_heads[deck_idx][i], cue_head_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    }

    for (int i = 0; i < 4; i++) {
        s_beat_pulses[deck_idx][i] = lv_obj_create(panel->panel);
        lv_obj_set_size(s_beat_pulses[deck_idx][i], 12, 12);
        int pulse_y = (deck_idx == CTRL_DECK_1) ? 288 : 300;
        int pulse_x = 0;
        if (i == 0) pulse_x = 358;
        else if (i == 1) pulse_x = 382;
        else if (i == 2) pulse_x = 418;
        else if (i == 3) pulse_x = 442;
        lv_obj_set_pos(s_beat_pulses[deck_idx][i], pulse_x, pulse_y);
        lv_obj_set_style_radius(s_beat_pulses[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_beat_pulses[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_beat_pulses[deck_idx][i], COL_PANEL_DK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[deck_idx][i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[deck_idx][i], COL_BORDER_LT, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_beat_pulses[deck_idx][i], 1, LV_PART_MAIN);
    }

    ui_overview_compact_button(panel->panel, deck, 4, top_y + 60, 76, "PLAY", &s_style_btn_primary, play_pause_event_cb);
    ui_overview_compact_button(panel->panel, deck, 4, top_y + 102, 76, "CUE", &s_style_btn_amber, cue_event_cb);
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
    const int fx_x = 736;
    const int fx_w = 64;
    const int slot_x = 4;
    const int slot_w = 56;

    s_overview_fx_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_overview_fx_panel);
    lv_obj_add_style(s_overview_fx_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_overview_fx_panel, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overview_fx_panel, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overview_fx_panel, 1, LV_PART_MAIN);
    lv_obj_set_size(s_overview_fx_panel, fx_w, 316);
    lv_obj_set_pos(s_overview_fx_panel, fx_x, 0);
    lv_obj_clear_flag(s_overview_fx_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = ui_overview_bar(s_overview_fx_panel, 0, 0, fx_w, 28, COL_PANEL);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    ui_fx_panel_label(s_overview_fx_panel, "BEAT FX", 0, 7, fx_w,
                      &lv_font_montserrat_12, COL_TEXT);

    const char *values[] = { "ECHO", "ECHO", "REVERB" };
    const int y0[] = { 30, 120, 210 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *slot = ui_overview_bar(s_overview_fx_panel, slot_x, y0[i], slot_w, 58, lv_color_hex(0x263033));
        lv_obj_set_style_border_width(slot, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(slot, COL_BORDER, LV_PART_MAIN);
        lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);

        ui_overview_bar(s_overview_fx_panel, slot_x, y0[i] + 54, slot_w, 5,
                        i == 1 ? lv_color_hex(0x146B17) : lv_color_hex(0x18F72B));
        ui_fx_panel_label(s_overview_fx_panel, values[i], slot_x, y0[i] + 21, slot_w,
                          &lv_font_montserrat_12, COL_ACCENT);

        lv_obj_t *off = ui_overview_bar(s_overview_fx_panel, slot_x, y0[i] + 62, slot_w, 26, COL_BG);
        lv_obj_set_style_border_width(off, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(off, COL_BORDER, LV_PART_MAIN);
        lv_obj_remove_flag(off, LV_OBJ_FLAG_CLICKABLE);
        ui_fx_panel_label(s_overview_fx_panel, "OFF", 0, y0[i] + 68, fx_w,
                          &lv_font_montserrat_12, COL_TEXT_DIM);
    }
}

static void ui_create_overview_center_marker(lv_obj_t *parent)
{
    lv_obj_t *line = ui_overview_bar(parent, OVERVIEW_WAVE_CENTER_X, 0, 1, 316, COL_TEXT);
    lv_obj_set_style_bg_opa(line, LV_OPA_80, LV_PART_MAIN);

    lv_obj_t *top = ui_overview_bar(parent, OVERVIEW_WAVE_CENTER_X - 4, 0, 9, 2, COL_TEXT);
    lv_obj_set_style_bg_opa(top, LV_OPA_80, LV_PART_MAIN);
    lv_obj_t *bottom = ui_overview_bar(parent, OVERVIEW_WAVE_CENTER_X - 4, OVERVIEW_DECK2_WAVE_Y - 2, 9, 2, COL_TEXT);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_80, LV_PART_MAIN);

}

#if 0
static void ui_create_overview_phase_meter(lv_obj_t *parent)
{
    s_phase_meter_label = ui_overview_value_label(parent, &lv_font_montserrat_12,
                                                  COL_TEXT_MUTED,
                                                  OVERVIEW_PHASE_X,
                                                  OVERVIEW_PHASE_Y - 18,
                                                  OVERVIEW_PHASE_W,
                                                  "PHASE --");
    lv_obj_set_style_text_align(s_phase_meter_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(s_phase_meter_label, LV_OBJ_FLAG_HIDDEN);

    s_phase_meter_track = ui_overview_bar(parent,
                                          OVERVIEW_PHASE_X,
                                          OVERVIEW_PHASE_Y,
                                          OVERVIEW_PHASE_W,
                                          4,
                                          COL_PANEL_DK);
    lv_obj_set_style_bg_opa(s_phase_meter_track, LV_OPA_COVER, LV_PART_MAIN);

    s_phase_meter_center = ui_overview_bar(parent,
                                           OVERVIEW_PHASE_X + (OVERVIEW_PHASE_W / 2),
                                           OVERVIEW_PHASE_Y - 5,
                                           1,
                                           14,
                                           COL_TEXT);
    lv_obj_set_style_bg_opa(s_phase_meter_center, LV_OPA_80, LV_PART_MAIN);

    s_phase_meter_knob = ui_overview_bar(parent,
                                         OVERVIEW_PHASE_X + (OVERVIEW_PHASE_W / 2) -
                                             (OVERVIEW_PHASE_KNOB_W / 2),
                                         OVERVIEW_PHASE_Y - 6,
                                         OVERVIEW_PHASE_KNOB_W,
                                         16,
                                         COL_TEXT_DIM);
}
#endif

lv_obj_t *ui_overview_create(lv_obj_t *parent) {
    lv_obj_t *screen = NULL;
    screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_add_style(screen, &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(screen, UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(screen, 0, UI_CONTENT_Y);

    ui_create_overview_deck_panel(screen, CTRL_DECK_1, 4);
    ui_create_overview_deck_panel(screen, CTRL_DECK_2, 222);
    ui_create_overview_center_marker(screen);
    // ui_create_overview_phase_meter(screen);
    ui_create_overview_fx_panel(screen);
    return screen;
}


// ─── Waveform Helpers ────────────────────────────────────────────────────────

static uint32_t ui_overview_main_window_ms(uint8_t deck, const anlz_metadata_t *meta)
{
    uint32_t beat_ms = 0;
    if (meta && meta->beats && meta->beat_count > 0 && meta->beats[0].bpm_x100 > 0) {
        beat_ms = 6000000u / meta->beats[0].bpm_x100;
    } else {
        uint16_t bpm = s_overview_deck_bpm[ui_overview_deck_index(deck)];
        if (bpm > 0) {
            beat_ms = 60000u / bpm;
        }
    }

    if (beat_ms == 0) {
        beat_ms = 500u;
    }

    uint32_t window_ms = beat_ms * OVERVIEW_MAIN_VISIBLE_BEATS;
    if (window_ms < OVERVIEW_MAIN_MIN_WINDOW_MS) {
        window_ms = OVERVIEW_MAIN_MIN_WINDOW_MS;
    }
    if (window_ms > OVERVIEW_MAIN_MAX_WINDOW_MS) {
        window_ms = OVERVIEW_MAIN_MAX_WINDOW_MS;
    }
    return window_ms;
}

#ifndef WIN32
static bool ui_overview_wave_overlay_ensure_buffer(uint8_t idx)
{
    if (idx >= DECK_CORE_DECK_COUNT) {
        return false;
    }
    if (s_overview_wave_overlay_rgb565[idx]) {
        if (!s_overview_wave_cache[idx].pixels) {
            (void)ui_overview_wave_cache_bind_strip(&s_overview_wave_cache[idx],
                                                    s_overview_wave_overlay_rgb565[idx],
                                                    OVERVIEW_WAVE_STRIP_W,
                                                    OVERVIEW_WAVE_STRIP_W,
                                                    OVERVIEW_CV_W,
                                                    OVERVIEW_CV_H,
                                                    OVERVIEW_WAVE_STRIP_MARGIN_PX,
                                                    s_overview_wave_rgb565_palette,
                                                    sizeof(s_overview_wave_rgb565_palette) /
                                                        sizeof(s_overview_wave_rgb565_palette[0]));
        }
        return true;
    }

    size_t bytes = (size_t)OVERVIEW_WAVE_STRIP_W * OVERVIEW_CV_H * sizeof(uint16_t);
    s_overview_wave_overlay_rgb565[idx] =
        ui_lvgl_backend_alloc_dma_buffer(bytes, &s_overview_wave_overlay_bytes);
    if (!s_overview_wave_overlay_rgb565[idx]) {
        ESP_LOGW(TAG, "D%u overview overlay RGB565 buffer alloc failed (%u bytes)",
                 (unsigned)(idx + 1u),
                 (unsigned)s_overview_wave_overlay_bytes);
        return false;
    }

    memset(s_overview_wave_overlay_rgb565[idx], 0, s_overview_wave_overlay_bytes);
    ESP_LOGI(TAG,
             "D%u overview waveform strip: visible=%dx%d strip=%dx%d bytes=%u",
             (unsigned)(idx + 1u),
             OVERVIEW_CV_W,
             OVERVIEW_CV_H,
             OVERVIEW_WAVE_STRIP_W,
             OVERVIEW_CV_H,
             (unsigned)s_overview_wave_overlay_bytes);
    (void)ui_overview_wave_cache_bind_strip(&s_overview_wave_cache[idx],
                                            s_overview_wave_overlay_rgb565[idx],
                                            OVERVIEW_WAVE_STRIP_W,
                                            OVERVIEW_WAVE_STRIP_W,
                                            OVERVIEW_CV_W,
                                            OVERVIEW_CV_H,
                                            OVERVIEW_WAVE_STRIP_MARGIN_PX,
                                            s_overview_wave_rgb565_palette,
                                            sizeof(s_overview_wave_rgb565_palette) /
                                                sizeof(s_overview_wave_rgb565_palette[0]));
    return true;
}

static bool ui_overview_wave_overlay_rect(const ui_overview_deck_panel_t *panel,
                                          ui_overlay_rect_t *logical)
{
    if (!panel || !panel->wave_border || !logical) {
        return false;
    }

    lv_area_t area;
    lv_obj_get_coords(panel->wave_border, &area);
    *logical = (ui_overlay_rect_t){
        .x = area.x1 + OVERVIEW_WAVE_INSET_X,
        .y = area.y1 + OVERVIEW_WAVE_INSET_Y,
        .w = OVERVIEW_CV_W,
        .h = OVERVIEW_CV_H,
    };
    return true;
}

static void ui_overview_overlay_perf_record(ui_overview_perf_counter_t *counter,
                                            uint8_t idx,
                                            const char *phase,
                                            uint32_t duration_us)
{
    if (!ui_diagnostics_enabled()) {
        return;
    }

    ui_overview_perf_report_t report;
    if (ui_overview_perf_record(counter, duration_us, &report)) {
        ESP_LOGI(TAG,
                 "D%u overview overlay %s: last=%u us avg=%u us max=%u us samples=%u",
                 (unsigned)(idx + 1u),
                 phase,
                 (unsigned)report.last_us,
                 (unsigned)report.avg_us,
                 (unsigned)report.max_us,
                 (unsigned)report.samples);
    }
}

static void ui_overview_blit_wave_chrome_rgb565(const ui_overlay_rect_t *logical,
                                                uint8_t idx)
{
    if (!logical) {
        return;
    }

    const uint16_t playhead_color = UI_RGB565(0x00, 0xFF, 0x00); // Bright green

    // Playhead (middle line)
    ui_overlay_rect_t rect = {
        .x = logical->x + (logical->w / 2) - (OVERVIEW_PLAYHEAD_W / 2),
        .y = logical->y,
        .w = OVERVIEW_PLAYHEAD_W,
        .h = logical->h,
    };
    ui_lvgl_backend_draw_rect_rgb565(&rect, playhead_color);
}

static bool ui_overview_blit_wave_overlay_rgb565(ui_overview_deck_panel_t *panel,
                                                 uint8_t deck,
                                                 uint16_t *src,
                                                 const ui_overview_wave_cache_report_t *cache_report,
                                                 ui_lvgl_backend_blit_perf_t *out_perf)
{
    uint8_t idx = ui_overview_deck_index(deck);
    if (idx >= DECK_CORE_DECK_COUNT || s_overview_active_tab != 0 ||
        !src || !cache_report || cache_report->blit_count == 0) {
        return false;
    }

    ui_overlay_rect_t logical;
    if (!ui_overview_wave_overlay_rect(panel, &logical)) {
        return false;
    }

    ui_lvgl_backend_blit_perf_t total_perf = {0};
    for (uint8_t seg_i = 0; seg_i < cache_report->blit_count; seg_i++) {
        const ui_overview_wave_cache_blit_t *seg = &cache_report->blit[seg_i];
        if (seg->width_px == 0) {
            continue;
        }

        ui_overlay_rect_t seg_logical = logical;
        seg_logical.x += seg->dst_x_px;
        seg_logical.w = seg->width_px;

        ui_lvgl_backend_blit_perf_t perf = {0};
        esp_err_t err = ui_lvgl_backend_blit_rgb565_ppa270_region(&seg_logical,
                                                                  src,
                                                                  OVERVIEW_WAVE_STRIP_W,
                                                                  OVERVIEW_CV_H,
                                                                  seg->src_x_px,
                                                                  0,
                                                                  seg->width_px,
                                                                  OVERVIEW_CV_H,
                                                                  s_overview_wave_overlay_bytes,
                                                                  &perf);
        total_perf.msync_us += perf.msync_us;
        total_perf.ppa_us += perf.ppa_us;
        total_perf.total_us += perf.total_us;
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "D%u overview overlay PPA failed: %s seg=%u logical=(%d,%d %dx%d) src_x=%u",
                     (unsigned)(idx + 1u),
                     esp_err_to_name(err),
                     (unsigned)seg_i,
                     seg_logical.x, seg_logical.y, seg_logical.w, seg_logical.h,
                     (unsigned)seg->src_x_px);
            return false;
        }
    }
    ui_overview_blit_wave_chrome_rgb565(&logical, idx);
    if (out_perf) {
        *out_perf = total_perf;
    }
    ui_overview_overlay_perf_record(&s_overview_overlay_msync_perf[idx],
                                     idx,
                                     "msync",
                                     total_perf.msync_us);
    ui_overview_overlay_perf_record(&s_overview_overlay_ppa_perf[idx],
                                     idx,
                                     "ppa",
                                     total_perf.ppa_us);
    ui_overview_overlay_perf_record(&s_overview_overlay_total_perf[idx],
                                     idx,
                                     "total",
                                     total_perf.total_us);

    return true;
}

#endif

static void ui_render_overview_main_waveform(ui_overview_deck_panel_t *panel,
                                             uint8_t deck,
                                             const ui_waveform_source_t *source,
                                             uint32_t duration_ms,
                                             const anlz_metadata_t *meta,
                                             uint32_t center_ms,
                                             uint32_t window_ms)
{
    if (!panel) {
        return;
    }

#ifndef WIN32
    uint8_t idx = ui_overview_deck_index(deck);
    if (idx < DECK_CORE_DECK_COUNT &&
        ui_overview_scheduler_direct_overlay_allowed(idx) &&
        ui_overview_wave_overlay_ensure_buffer(idx)) {
        uint16_t *overlay = s_overview_wave_overlay_rgb565[idx];
        ui_overview_wave_cache_report_t cache_report = {0};
        int64_t render_start_us = ui_diagnostics_enabled() ? esp_timer_get_time() : 0;
        bool cache_updated = ui_overview_wave_cache_update(&s_overview_wave_cache[idx],
                                                           source,
                                                           duration_ms,
                                                           meta,
                                                           center_ms,
                                                           window_ms,
                                                           &cache_report);
        uint32_t cache_us = 0;
        if (ui_diagnostics_enabled()) {
            int64_t elapsed_us = esp_timer_get_time() - render_start_us;
            if (elapsed_us > 0) {
                cache_us = (uint32_t)elapsed_us;
            }
        }
        if (!cache_updated || !cache_report.blit_required) {
            return;
        }
        ui_lvgl_backend_blit_perf_t blit_perf = {0};
        if (!ui_overview_blit_wave_overlay_rgb565(panel, deck, overlay,
                                                  &cache_report,
                                                  &blit_perf)) {
            return;
        }
        if (ui_diagnostics_enabled()) {
            ui_overview_perf_report_t report;
            if (ui_overview_perf_record(&s_overview_wave_perf[idx],
                                        cache_us,
                                        &report)) {
                ESP_LOGI(TAG,
                         "D%u overview main cache: kind=%u dx=%d cols=%u blits=%u cache_last=%u us cache_avg=%u us cache_max=%u us ppa_us=%u samples=%u",
                         (unsigned)(idx + 1u),
                         (unsigned)cache_report.kind,
                         cache_report.scroll_dx_px,
                         (unsigned)cache_report.columns_rendered,
                         (unsigned)cache_report.blit_count,
                         (unsigned)report.last_us,
                         (unsigned)report.avg_us,
                         (unsigned)report.max_us,
                         (unsigned)blit_perf.ppa_us,
                         (unsigned)report.samples);
            }
        }
    } else {
        return;
    }
#else
    if (!panel->wave_buf) {
        return;
    }
    const int W = OVERVIEW_CV_W;
    const int H = OVERVIEW_CV_H;
    uint8_t *buf = panel->wave_buf + 256 * sizeof(lv_color32_t);
    const int S = panel->wave_stride_px;
    ui_overview_renderer_draw_main(buf, S, W, H, source, duration_ms, meta,
                                   center_ms, window_ms);
    lv_obj_invalidate(panel->wave_canvas);
#endif
    panel->last_wave_center_ms = center_ms;
    panel->last_wave_window_ms = window_ms;
}

/* Render the overview waveform to the canvas once at track load. */
void ui_overview_load_waveform_data(uint8_t deck,
                                  uint32_t duration_ms,
                                  const uint8_t waveform_low[400],
                                  bool has_waveform,
                                  const anlz_metadata_t *meta)
{
    uint8_t idx = ui_overview_deck_index(deck);
    s_overview_deck_duration_ms[idx] = duration_ms;
    s_overview_deck_meta[idx] = meta;
    s_overview_wave_source[idx] = (ui_overview_waveform_source_info_t){
        .kind = has_waveform ? UI_OVERVIEW_WAVEFORM_SOURCE_LOADED_MEDIA
                             : UI_OVERVIEW_WAVEFORM_SOURCE_METADATA,
        .waveform_low = waveform_low,
        .has_waveform = has_waveform,
    };
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    panel->last_mini_fill_x = -1;
    panel->last_playhead_x = -1;
    panel->last_wave_center_ms = UINT32_MAX;
    panel->last_wave_window_ms = 0;
    panel->last_time_bucket = UINT32_MAX;
    panel->last_remain_bucket = UINT32_MAX;
#ifndef WIN32
    ui_overview_wave_cache_reset(&s_overview_wave_cache[idx]);
#endif
    ui_waveform_source_t wave_source =
        ui_waveform_source_select(meta, waveform_low, has_waveform);
    bool wave_valid = wave_source.kind != UI_WAVEFORM_SOURCE_NONE && duration_ms > 0;

    uint32_t window_ms = ui_overview_main_window_ms(deck, meta);
    if (ui_overview_main_wave_ready(panel)) {
        ui_render_overview_main_waveform(panel, deck, &wave_source, duration_ms, meta,
                                         0, window_ms);
    }

    if (panel->mini_wave_canvas && panel->mini_wave_buf) {
        uint8_t *mini_buf = panel->mini_wave_buf + 256 * sizeof(lv_color32_t);
        const int MW = OVERVIEW_MINI_CV_W;
        const int MH = OVERVIEW_MINI_CV_H;
        const int MS = panel->mini_wave_stride_px;
        ui_overview_renderer_draw_mini(mini_buf, MS, MW, MH,
                                       wave_valid ? &wave_source : NULL,
                                       duration_ms);

        ui_overview_invalidate_mini_wave_range(panel, 0, OVERVIEW_MINI_CV_W);
    }
}

void ui_overview_update_cue_markers(uint8_t deck, const anlz_metadata_t *meta, uint32_t duration_ms)
{
    uint8_t deck_idx = ui_overview_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[deck_idx];
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
            if (s_overview_cue_heads[deck_idx][i]) {
                lv_obj_add_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    uint32_t window_ms = panel->last_wave_window_ms > 0
                       ? panel->last_wave_window_ms
                       : ui_overview_main_window_ms(deck, meta);
    uint32_t center_ms = panel->last_wave_center_ms != UINT32_MAX
                       ? panel->last_wave_center_ms
                       : 0;
    int64_t window_start_ms = (int64_t)center_ms - ((int64_t)window_ms / 2);
    int64_t window_end_ms = window_start_ms + (int64_t)window_ms;

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
            if (s_overview_cue_heads[deck_idx][i]) {
                lv_obj_add_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }

        if ((int64_t)pos < window_start_ms || (int64_t)pos > window_end_ms) {
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
            if (s_overview_cue_heads[deck_idx][i]) {
                lv_obj_add_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }

        int marker_x = OVERVIEW_WAVE_INSET_X +
            (int)((((int64_t)pos - window_start_ms) * OVERVIEW_CV_W) /
                  (int64_t)window_ms) - 1;
        if (marker_x < OVERVIEW_WAVE_INSET_X - 1) marker_x = OVERVIEW_WAVE_INSET_X - 1;
        if (marker_x > OVERVIEW_WAVE_INSET_X + OVERVIEW_CV_W - 2) {
            marker_x = OVERVIEW_WAVE_INSET_X + OVERVIEW_CV_W - 2;
        }
        lv_obj_set_pos(marker, marker_x, OVERVIEW_WAVE_INSET_Y + 4);
        lv_obj_set_style_bg_color(marker, lv_color_hex(cue_hex_colors[i]), LV_PART_MAIN);
        lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
        if (s_overview_cue_heads[deck_idx][i]) {
            int head_x = marker_x - 2;
            int head_min = OVERVIEW_WAVE_INSET_X;
            int head_max = OVERVIEW_WAVE_INSET_X + OVERVIEW_CV_W - 7;
            if (head_x < head_min) {
                head_x = head_min;
            } else if (head_x > head_max) {
                head_x = head_max;
            }
            lv_obj_set_pos(s_overview_cue_heads[deck_idx][i], head_x, OVERVIEW_WAVE_INSET_Y);
            lv_obj_set_style_bg_color(s_overview_cue_heads[deck_idx][i],
                                      lv_color_hex(cue_hex_colors[i]), LV_PART_MAIN);
            lv_obj_remove_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}


typedef struct {
    bool valid;
    bool active;
    bool downbeat;
    lv_opa_t opa;
} ui_overview_beat_dot_cache_t;

static ui_overview_beat_dot_cache_t s_cache_beat_dots[DECK_CORE_DECK_COUNT][4];

static void ui_update_beat_indicator(uint8_t deck_idx, const ui_beat_indicator_state_t *state)
{
    if (deck_idx >= DECK_CORE_DECK_COUNT) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (!s_beat_pulses[deck_idx][i]) {
            continue;
        }

        bool active = state && state->valid && state->phase == (uint8_t)i;
        bool downbeat = active && state->downbeat;
        lv_opa_t opa = active ? LV_OPA_COVER : LV_OPA_40;

        ui_overview_beat_dot_cache_t *cache = &s_cache_beat_dots[deck_idx][i];
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
            lv_obj_set_style_bg_color(s_beat_pulses[deck_idx][i], lv_color_hex(0x30343B), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_beat_pulses[deck_idx][i], LV_OPA_40, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_beat_pulses[deck_idx][i], lv_color_hex(0x4A515C), LV_PART_MAIN);
            continue;
        }

        // Beat-indicator colours stay inline (paired with the downbeat red, not chrome).
        lv_color_t color = downbeat ? lv_color_hex(0xFF1744) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_bg_color(s_beat_pulses[deck_idx][i], color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[deck_idx][i], opa, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[deck_idx][i], color, LV_PART_MAIN);
    }
}

static void ui_update_phase_meter(const deck_state_t *deck1_state,
                                  const deck_state_t *deck2_state)
{
    if (!s_phase_meter_label || !s_phase_meter_knob) {
        return;
    }

    const ui_deck_track_info_t *info1 = s_overview_deck_info[ui_overview_deck_index(CTRL_DECK_1)];
    const ui_deck_track_info_t *info2 = s_overview_deck_info[ui_overview_deck_index(CTRL_DECK_2)];
    bool tracks_ready = info1 && info1->valid && info2 && info2->valid;
    if (!deck1_state || !deck2_state || !tracks_ready) {
        ui_label_set_text_if_changed(s_phase_meter_label, "PHASE --");
        ui_obj_set_text_color_if_changed(s_phase_meter_label, COL_TEXT_MUTED);
        ui_obj_set_x_if_changed(s_phase_meter_knob,
                                OVERVIEW_PHASE_X + (OVERVIEW_PHASE_W / 2) -
                                    (OVERVIEW_PHASE_KNOB_W / 2));
        ui_obj_set_bg_color_if_changed(s_phase_meter_knob, COL_TEXT_DIM);
        ui_obj_set_bg_opa_if_changed(s_phase_meter_knob, LV_OPA_60);
        return;
    }

    const anlz_metadata_t *meta1 = s_overview_deck_meta[ui_overview_deck_index(CTRL_DECK_1)];
    const anlz_metadata_t *meta2 = s_overview_deck_meta[ui_overview_deck_index(CTRL_DECK_2)];
    ui_beat_indicator_state_t beat1 =
        ui_beat_indicator_calculate(deck1_state->position_ms,
                                    meta1 ? meta1->beats : NULL,
                                    meta1 ? meta1->beat_count : 0,
                                    s_overview_deck_bpm[ui_overview_deck_index(CTRL_DECK_1)]);
    ui_beat_indicator_state_t beat2 =
        ui_beat_indicator_calculate(deck2_state->position_ms,
                                    meta2 ? meta2->beats : NULL,
                                    meta2 ? meta2->beat_count : 0,
                                    s_overview_deck_bpm[ui_overview_deck_index(CTRL_DECK_2)]);
    ui_beat_phase_delta_t delta = ui_beat_phase_delta_calculate(beat1, beat2);
    if (!delta.valid) {
        ui_label_set_text_if_changed(s_phase_meter_label, "PHASE --");
        ui_obj_set_text_color_if_changed(s_phase_meter_label, COL_TEXT_MUTED);
        ui_obj_set_x_if_changed(s_phase_meter_knob,
                                OVERVIEW_PHASE_X + (OVERVIEW_PHASE_W / 2) -
                                    (OVERVIEW_PHASE_KNOB_W / 2));
        ui_obj_set_bg_color_if_changed(s_phase_meter_knob, COL_TEXT_DIM);
        ui_obj_set_bg_opa_if_changed(s_phase_meter_knob, LV_OPA_60);
        return;
    }

    int knob_center = OVERVIEW_PHASE_X + (OVERVIEW_PHASE_W / 2) +
        ((int)delta.deck2_delta_permille * (OVERVIEW_PHASE_W / 2)) / 2000;
    int min_center = OVERVIEW_PHASE_X;
    int max_center = OVERVIEW_PHASE_X + OVERVIEW_PHASE_W;
    if (knob_center < min_center) knob_center = min_center;
    if (knob_center > max_center) knob_center = max_center;
    ui_obj_set_x_if_changed(s_phase_meter_knob, knob_center - (OVERVIEW_PHASE_KNOB_W / 2));

    lv_color_t color = COL_GREEN;
    if (!delta.locked) {
        color = delta.deck2_delta_permille < 0 ? COL_AMBER : COL_ACCENT;
    }
    ui_obj_set_bg_color_if_changed(s_phase_meter_knob, color);
    ui_obj_set_bg_opa_if_changed(s_phase_meter_knob, LV_OPA_COVER);
    ui_obj_set_text_color_if_changed(s_phase_meter_label, color);

    if (delta.locked) {
        ui_label_set_text_if_changed(s_phase_meter_label, "PHASE LOCK");
    } else {
        int abs_delta = delta.deck2_delta_permille < 0 ?
            -delta.deck2_delta_permille : delta.deck2_delta_permille;
        char text[16];
        snprintf(text, sizeof(text),
                 "D2 %c%d.%02dB",
                 delta.deck2_delta_permille < 0 ? '-' : '+',
                 abs_delta / 1000,
                 (abs_delta % 1000) / 10);
        ui_label_set_text_if_changed(s_phase_meter_label, text);
    }
}

static ui_waveform_source_t ui_overview_redraw_source(uint8_t deck,
                                                      const anlz_metadata_t *meta)
{
    uint8_t idx = ui_overview_deck_index(deck);
    if (s_overview_wave_source[idx].kind == UI_OVERVIEW_WAVEFORM_SOURCE_LOADED_MEDIA) {
        return ui_waveform_source_select_for_overview_redraw(
            meta,
            s_overview_wave_source[idx].waveform_low,
            s_overview_wave_source[idx].has_waveform);
    }
    return ui_waveform_source_select_for_overview_redraw(meta, NULL, false);
}

static void ui_update_overview_waveform_progress(uint8_t deck,
                                                 ui_overview_deck_panel_t *panel,
                                                 uint32_t position_ms,
                                                 uint32_t duration_ms,
                                                 bool playing)
{
    if (!panel || duration_ms == 0) return;

    float progress = (float)position_ms / (float)duration_ms;
    if (progress > 1.0f) progress = 1.0f;

    int main_playhead_x = (OVERVIEW_CV_W / 2) - (OVERVIEW_PLAYHEAD_W / 2);
    if (panel->playhead && main_playhead_x != panel->last_playhead_x) {
        lv_obj_set_pos(panel->playhead, OVERVIEW_WAVE_INSET_X + main_playhead_x, OVERVIEW_WAVE_INSET_Y);
        panel->last_playhead_x = main_playhead_x;
    }

    const anlz_metadata_t *meta = s_overview_deck_meta[ui_overview_deck_index(deck)];
    uint32_t window_ms = ui_overview_main_window_ms(deck, meta);
    uint32_t center_ms = position_ms > duration_ms ? duration_ms : position_ms;
    center_ms = ui_overview_motion_snap_center_ms(center_ms, window_ms, OVERVIEW_CV_W);
    if (center_ms > duration_ms) {
        center_ms = duration_ms;
    }
    ui_waveform_source_t source = ui_overview_redraw_source(deck, meta);
    bool redraw_main = ui_overview_motion_should_redraw(panel->last_wave_center_ms,
                                                        panel->last_wave_window_ms,
                                                        center_ms,
                                                        window_ms,
                                                        source.kind,
                                                        playing);

    if (redraw_main && ui_overview_main_wave_ready(panel) &&
        source.kind != UI_WAVEFORM_SOURCE_NONE) {
#ifndef WIN32
        if (!ui_overview_scheduler_try_consume_main_redraw(&s_overview_scheduler)) {
            redraw_main = false;
        }
#endif
    }

    if (redraw_main && ui_overview_main_wave_ready(panel) &&
        source.kind != UI_WAVEFORM_SOURCE_NONE) {
        ui_render_overview_main_waveform(panel, deck, &source, duration_ms, meta,
                                         center_ms, window_ms);
    }

    if (!panel->mini_wave_canvas || !panel->mini_wave_buf) {
        return;
    }

    int mini_x = (int)(progress * (float)OVERVIEW_MINI_CV_W);
    if (mini_x < 0) mini_x = 0;
    if (mini_x > OVERVIEW_MINI_CV_W) mini_x = OVERVIEW_MINI_CV_W;
    if (panel->mini_playhead) {
        ui_obj_set_x_if_changed(panel->mini_playhead, mini_x);
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
    ui_overview_invalidate_mini_wave_range(panel, mx0, mx1);
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

        char text[16];
        if (panel->label_ch) {
            snprintf(text, sizeof(text), "CH %3u%%", (unsigned)view.fader_pct);
            ui_label_set_text_if_changed(panel->label_ch, text);
        }
        if (panel->label_out) {
            snprintf(text, sizeof(text), "OUT %3u%%", (unsigned)view.output_pct);
            ui_label_set_text_if_changed(panel->label_out, text);
        }
        if (s_overview_deck_pfl[deck] != view.pfl_on) {
            s_overview_deck_pfl[deck] = view.pfl_on;
            ui_overview_apply_deck_badge(deck);
        }
        if (panel->out_bar_fill) {
            int w = (78 * (int)view.output_pct) / 100;
            if (w < 2) w = 2;
            if (lv_obj_get_width(panel->out_bar_fill) != w) {
                lv_obj_set_width(panel->out_bar_fill, w);
            }
        }
    }
}
#endif

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

static void ui_update_overview_deck(uint8_t deck, const deck_state_t *state)
{
    uint8_t idx = ui_overview_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    if (!panel->panel || !state) return;

    uint32_t duration_ms = s_overview_deck_duration_ms[idx];
    uint32_t elapsed_ms = ui_position_interpolator_update(
        &s_overview_position_interp[idx],
        state->position_ms,
        duration_ms,
        state->playing,
        ui_pitch_speed_permille(state),
        ui_monotonic_time_us());
    uint32_t remain_ms = (duration_ms > elapsed_ms) ? (duration_ms - elapsed_ms) : 0;
    const ui_deck_track_info_t *info = s_overview_deck_info[idx];
    ui_deck_track_info_t empty_info = {0};
    if (!info) {
        info = &empty_info;
    }


    if (s_overview_deck_playing[idx] != state->playing) {
        s_overview_deck_playing[idx] = state->playing;
        ui_overview_apply_deck_badge(deck);
    }
    ui_label_set_text_if_changed(panel->label_title, info->valid ? info->title : "No Track");

    uint32_t time_bucket = remain_ms / OVERVIEW_TIME_UPDATE_MS;
    if (time_bucket != panel->last_time_bucket) {
        char mins_text[8];
        char secs_text[4];
        char fraction_text[8];
        panel->last_time_bucket = time_bucket;
        uint32_t display_remain_ms = time_bucket * OVERVIEW_TIME_UPDATE_MS;
        snprintf(mins_text, sizeof(mins_text), "-%02u",
                 (unsigned)(display_remain_ms / 60000));
        snprintf(secs_text, sizeof(secs_text), "%02u",
                 (unsigned)((display_remain_ms % 60000) / 1000));
        snprintf(fraction_text, sizeof(fraction_text), ".%02u",
                 (unsigned)((display_remain_ms % 1000) / 10));
        ui_label_set_text_if_changed(panel->label_time, mins_text);
        ui_label_set_text_if_changed(panel->label_time_secs, secs_text);
        ui_label_set_text_if_changed(panel->label_time_fraction, fraction_text);
    }

    uint32_t remain_bucket = remain_ms / 1000u;
    if (remain_bucket != panel->last_remain_bucket) {
        char text[16];
        panel->last_remain_bucket = remain_bucket;
        snprintf(text, sizeof(text), "-%02u:%02u.%02u",
                 (unsigned)(remain_ms / 60000),
                 (unsigned)((remain_ms % 60000) / 1000),
                 (unsigned)((remain_ms % 1000) / 10));
        ui_label_set_text_if_changed(panel->label_remain, text);
    }

    float pitch_pct;
#ifndef WIN32
    pitch_pct = audio_engine_raw_pitch_to_percent(state->pitch);
#else
    pitch_pct = ((8192.0f - (float)state->pitch) / 8192.0f) * 10.0f;
#endif
    uint16_t base_bpm = s_overview_deck_bpm[idx];
    float current_bpm = (float)(base_bpm ? base_bpm : 120) * (1.0f + (pitch_pct / 100.0f));
    char bpm_text[8];
    snprintf(bpm_text, sizeof(bpm_text), "%u", (unsigned)(current_bpm + 0.5f));
    ui_label_set_text_if_changed(panel->label_bpm, bpm_text);
    int pc = (int)(pitch_pct * 100.0f + (pitch_pct >= 0.0f ? 0.5f : -0.5f));
    char pitch_text[16];
    snprintf(pitch_text, sizeof(pitch_text), "%c%d.%02d%%",
             (pc < 0) ? '-' : '+',
             (pc < 0 ? -pc : pc) / 100,
             (pc < 0 ? -pc : pc) % 100);
    ui_label_set_text_if_changed(panel->label_pitch, pitch_text);

    ui_update_overview_waveform_progress(deck, panel, elapsed_ms, duration_ms,
                                         state->playing);
}


void ui_overview_set_performance_target(uint8_t active_deck)
{
    (void)active_deck;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        ui_overview_deck_panel_t *panel = &s_overview_decks[deck];
        if (!panel->panel || !panel->wave_border) {
            continue;
        }
        lv_obj_set_style_border_width(panel->wave_border, 0, LV_PART_MAIN);
    }
}

void ui_overview_init(const ui_overview_config_t *config)
{
    memset(&s_overview_config, 0, sizeof(s_overview_config));
    if (config) {
        s_overview_config = *config;
    }
    ui_overview_scheduler_init(&s_overview_scheduler);
}

void ui_overview_update(const ui_frame_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    s_overview_active_tab = ctx->active_tab;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        s_overview_deck_duration_ms[deck] = ctx->deck_duration_ms[deck];
        s_overview_deck_bpm[deck] = ctx->deck_bpm[deck];
        s_overview_deck_meta[deck] = ctx->deck_meta[deck];
        s_overview_deck_info[deck] = ctx->deck_info[deck];
        s_overview_wave_source[deck] = ctx->overview_wave_source[deck];
    }

    if (ctx->active_tab != 0) {
        return;
    }

#ifndef WIN32
    ui_overview_scheduler_begin_tick(
        &s_overview_scheduler,
        ui_overview_scheduler_budget_for_playing_decks(
            ctx->deck_state[CTRL_DECK_1].playing,
            ctx->deck_state[CTRL_DECK_2].playing));
#endif
    uint8_t first_deck = CTRL_DECK_1;
    uint8_t second_deck = CTRL_DECK_2;
    ui_overview_scheduler_next_deck_order(&s_overview_scheduler,
                                          CTRL_DECK_1,
                                          CTRL_DECK_2,
                                          &first_deck,
                                          &second_deck);

    ui_update_overview_deck(first_deck, &ctx->deck_state[first_deck]);
    ui_update_overview_deck(second_deck, &ctx->deck_state[second_deck]);

    static uint32_t last_beat_update_ms = 0;
    if (ctx->now_ms - last_beat_update_ms >= 30u || last_beat_update_ms == 0) {
        last_beat_update_ms = ctx->now_ms;
        for (uint8_t d = 0; d < DECK_CORE_DECK_COUNT; d++) {
            ui_beat_indicator_state_t beat_state = {0};
            bool beat_valid = false;
            if (ctx->deck_duration_ms[d] > 0) {
                beat_state = ui_beat_indicator_calculate(
                    ctx->deck_state[d].position_ms,
                    ctx->deck_meta[d] ? ctx->deck_meta[d]->beats : NULL,
                    ctx->deck_meta[d] ? ctx->deck_meta[d]->beat_count : 0,
                    ctx->deck_bpm[d]);
                beat_valid = beat_state.valid;
            }
            ui_update_beat_indicator(d, beat_valid ? &beat_state : NULL);
        }
    }

    if (ctx->overview_slow_update) {
#ifndef WIN32
        ui_update_mixer_overview(&ctx->mixer_snapshot);
#endif
        ui_update_phase_meter(&ctx->deck_state[CTRL_DECK_1], &ctx->deck_state[CTRL_DECK_2]);
        ui_overview_update_cue_markers(CTRL_DECK_1,
                                       ctx->deck_meta[CTRL_DECK_1],
                                       ctx->deck_duration_ms[CTRL_DECK_1]);
        ui_overview_update_cue_markers(CTRL_DECK_2,
                                       ctx->deck_meta[CTRL_DECK_2],
                                       ctx->deck_duration_ms[CTRL_DECK_2]);
    }
}
