#include "ui_overview.h"

#include "lvgl.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "deck_core.h"
#include "ui_beat_fx_format.h"
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
#include "ui_overview_window.h"
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

static void ui_obj_set_border_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
    if (!obj) return;
    if (lv_color_to_u32(lv_obj_get_style_border_color(obj, LV_PART_MAIN)) == lv_color_to_u32(color)) return;
    lv_obj_set_style_border_color(obj, color, LV_PART_MAIN);
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
#define OVERVIEW_BEAT_STRIP_DOT_SIZE_PX 12
#define OVERVIEW_BEAT_STRIP_STEP_PX 24
#define OVERVIEW_BEAT_STRIP_CENTER_GAP_PX 18
#define OVERVIEW_BEAT_STRIP_TOP_Y (OVERVIEW_DECK2_WAVE_Y + OVERVIEW_CV_H + 5)
#define OVERVIEW_BEAT_STRIP_ROW_GAP_PX 12
#define OVERVIEW_DECK_BADGE_X 4
#define OVERVIEW_DECK_BADGE_W 58
#define OVERVIEW_DECK_BADGE_H 38
#define OVERVIEW_DECK_BADGE_Y_OFFSET 12
#define OVERVIEW_TRANSPORT_X 4
#define OVERVIEW_TRANSPORT_W 58
#define OVERVIEW_TRANSPORT_PLAY_Y_OFFSET 60
#define OVERVIEW_TRANSPORT_CUE_Y_OFFSET 102
#define OVERVIEW_VU_X 68
/* 10 segments: the middle 8 keep their old positions; one segment is added at
 * the top and one at the bottom (Y_OFFSET moved up by one segment pitch, 17→3,
 * so the meter stays centred in the deck lane). */
#define OVERVIEW_VU_Y_OFFSET 3
#define OVERVIEW_VU_SEGMENT_W 10
#define OVERVIEW_VU_SEGMENT_H 11
#define OVERVIEW_VU_SEGMENT_GAP 3
#define OVERVIEW_VU_SEGMENT_COUNT 10
#define OVERVIEW_VU_H ((OVERVIEW_VU_SEGMENT_COUNT * OVERVIEW_VU_SEGMENT_H) + \
                       ((OVERVIEW_VU_SEGMENT_COUNT - 1) * OVERVIEW_VU_SEGMENT_GAP))
_Static_assert(OVERVIEW_DECK_BADGE_X + OVERVIEW_DECK_BADGE_W + 4 <= OVERVIEW_VU_X,
               "Deck badge must not overlap VU meter");
_Static_assert(OVERVIEW_VU_X >= (OVERVIEW_TRANSPORT_X + OVERVIEW_TRANSPORT_W + 4),
               "VU meter must not overlap transport buttons");
_Static_assert((OVERVIEW_VU_X + OVERVIEW_VU_SEGMENT_W + 4) <= OVERVIEW_WAVE_X,
               "VU meter must stay clear of waveform");
_Static_assert(OVERVIEW_VU_Y_OFFSET + OVERVIEW_VU_H <= OVERVIEW_CV_H,
               "VU meter must fit within the deck waveform lane height");
#define OVERVIEW_PLAYHEAD_W 3
#define OVERVIEW_OUTLINE_W 1
#define OVERVIEW_DECK_INFO_W 400
#define OVERVIEW_TITLE_Y 312
#define OVERVIEW_TITLE_H 30
#define OVERVIEW_TITLE_TEXT_W 392
#define OVERVIEW_INFO_DIVIDER_Y 344
#define OVERVIEW_INFO_ROW_Y 346
#define OVERVIEW_TIME_Y 354
#define OVERVIEW_MIX_ROW_Y 370
#define OVERVIEW_BPM_X 170
#define OVERVIEW_BPM_Y 348
#define OVERVIEW_BPM_W 92
#define OVERVIEW_BPM_TAG_X 264
/* Per-deck time counters on the BPM row (out of the blue title strip, which is
 * now title-only), at the BPM font size: elapsed at the title-aligned start, then
 * a gap, then remaining. Widths hold a full "MM:SS" / "-MM:SS" at montserrat_24
 * (worst-case ~69 / ~79 px) so neither clips into the other; both stay left of
 * the BPM value. */
#define OVERVIEW_TIME_X 8
#define OVERVIEW_ELAPSED_W 72
#define OVERVIEW_REMAIN_X 90
#define OVERVIEW_REMAIN_W 80
_Static_assert(OVERVIEW_REMAIN_X + OVERVIEW_REMAIN_W <= OVERVIEW_BPM_X, "overview time counters must stay left of the BPM value");
_Static_assert(OVERVIEW_TIME_X + OVERVIEW_ELAPSED_W <= OVERVIEW_REMAIN_X, "elapsed time must not overlap the remaining time");
#define OVERVIEW_PITCH_X 302
#define OVERVIEW_PITCH_Y 346
#define OVERVIEW_PITCH_CHIP_W 94
#define OVERVIEW_PITCH_CHIP_H 28
#define OVERVIEW_PITCH_W OVERVIEW_PITCH_CHIP_W
#define OVERVIEW_MINI_WAVE_Y 386
#define OVERVIEW_SIDE_BTN_H 38
/* The D1/D2 deck badges are sized to match the play/cue transport buttons. */
_Static_assert(OVERVIEW_DECK_BADGE_W == OVERVIEW_TRANSPORT_W, "deck badge width must match the play/cue buttons");
_Static_assert(OVERVIEW_DECK_BADGE_H == OVERVIEW_SIDE_BTN_H, "deck badge height must match the play/cue buttons");
#define OVERVIEW_FX_PANEL_X 736
#define OVERVIEW_FX_PANEL_Y 0
#define OVERVIEW_FX_PANEL_W 64
/* Panel runs from the top down to just above the blue title strip so the depth
 * meter fills the whole right rail with no dead space at the bottom. */
#define OVERVIEW_FX_PANEL_H 308
#define OVERVIEW_FX_ROW_X 4
#define OVERVIEW_FX_ROW_W 56
/* Effect identity chip (big, filled in the effect colour when FX is on). */
#define OVERVIEW_FX_CHIP_X 4
#define OVERVIEW_FX_CHIP_Y 28
#define OVERVIEW_FX_CHIP_W 56
#define OVERVIEW_FX_CHIP_H 36
/* Target channel pills (CH1 / CH2), lit in the effect colour when routed. */
#define OVERVIEW_FX_TARGET_CAPTION_Y 68
#define OVERVIEW_FX_PILL_Y 82
#define OVERVIEW_FX_PILL_W 25
#define OVERVIEW_FX_PILL_H 22
#define OVERVIEW_FX_PILL1_X 5
#define OVERVIEW_FX_PILL2_X 34
/* Beat-division chip. */
#define OVERVIEW_FX_BEAT_CAPTION_Y 110
#define OVERVIEW_FX_BEAT_CHIP_X 12
#define OVERVIEW_FX_BEAT_CHIP_Y 122
#define OVERVIEW_FX_BEAT_CHIP_W 40
#define OVERVIEW_FX_BEAT_CHIP_H 22
/* Depth is a vertical fill meter (fills bottom-up) — the live-ride element. */
#define OVERVIEW_FX_DEPTH_CAPTION_Y 150
#define OVERVIEW_FX_DEPTH_BAR_X 20
#define OVERVIEW_FX_DEPTH_BAR_Y 164
#define OVERVIEW_FX_DEPTH_BAR_W 24
#define OVERVIEW_FX_DEPTH_BAR_H 112
#define OVERVIEW_FX_DEPTH_VALUE_Y 282
_Static_assert(OVERVIEW_FX_DEPTH_VALUE_Y + 22 <= OVERVIEW_FX_PANEL_H,
               "FX depth value must stay inside the FX panel");
_Static_assert(OVERVIEW_FX_DEPTH_BAR_Y + OVERVIEW_FX_DEPTH_BAR_H <= OVERVIEW_FX_DEPTH_VALUE_Y,
               "FX depth meter must not overlap the depth value");
_Static_assert(OVERVIEW_FX_PILL2_X + OVERVIEW_FX_PILL_W <= OVERVIEW_FX_PANEL_W,
               "FX target pills must stay inside the FX panel");
_Static_assert(OVERVIEW_FX_PANEL_Y + OVERVIEW_FX_PANEL_H <= OVERVIEW_TITLE_Y,
               "FX panel must stay above the blue title strip");

static int ui_overview_beat_strip_offset_x(int phase)
{
    int center_delta = OVERVIEW_BEAT_STRIP_CENTER_GAP_PX;
    if (phase < 2) {
        center_delta = -OVERVIEW_BEAT_STRIP_CENTER_GAP_PX;
    }
    if (phase == 0 || phase == 3) {
        center_delta += (phase < 2) ? -OVERVIEW_BEAT_STRIP_STEP_PX
                                    : OVERVIEW_BEAT_STRIP_STEP_PX;
    }
    return center_delta - (OVERVIEW_BEAT_STRIP_DOT_SIZE_PX / 2);
}

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *label_deck;
    lv_obj_t *label_status;
    lv_obj_t *label_title;
    lv_obj_t *label_artist;
    lv_obj_t *title_time_bg;
    lv_obj_t *label_time_elapsed;
    lv_obj_t *label_time;
    lv_obj_t *label_bpm;
    lv_obj_t *label_pitch;
    lv_obj_t *label_ch;
    lv_obj_t *label_out;
    lv_obj_t *play_button;
    lv_obj_t *play_label;
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
    lv_obj_t *mini_played;
    lv_obj_t *mini_playhead;
    lv_obj_t *playhead;
    lv_obj_t *vu_segment[OVERVIEW_VU_SEGMENT_COUNT];
    int       last_mini_fill_x;
    int       last_mini_played_w;
    int       last_playhead_x;
    int       last_vu_level;
    uint32_t  last_wave_center_ms;
    uint32_t  last_wave_window_ms;
    uint32_t  last_time_bucket;
} ui_overview_deck_panel_t;

static ui_overview_deck_panel_t s_overview_decks[DECK_CORE_DECK_COUNT];
static bool s_overview_deck_pfl[DECK_CORE_DECK_COUNT];
static bool s_overview_deck_playing[DECK_CORE_DECK_COUNT];
static uint8_t s_overview_performance_target = CTRL_DECK_1;
static lv_obj_t *s_beat_pulses[DECK_CORE_DECK_COUNT][4];
static lv_obj_t *s_overview_cue_heads[DECK_CORE_DECK_COUNT][8];
static lv_obj_t *s_overview_fx_panel = NULL;
typedef struct {
    lv_obj_t *header;         /* header bar — effect-colour fill when FX is on   */
    lv_obj_t *header_label;   /* "FX"                                            */
    lv_obj_t *effect_chip;    /* filled identity chip behind the effect name     */
    lv_obj_t *effect;         /* effect name (FILTER / ECHO / FLANGER)           */
    lv_obj_t *pill_bg[2];     /* CH1 / CH2 target pills                          */
    lv_obj_t *pill_label[2];
    lv_obj_t *beat_chip;      /* beat-division chip outline                      */
    lv_obj_t *beat;           /* beat value (1/4 .. 4)                           */
    lv_obj_t *depth_bg;       /* vertical depth track                            */
    lv_obj_t *depth_fill;     /* vertical depth fill (bottom-up)                 */
    lv_obj_t *depth;          /* depth percentage                                */
} ui_overview_fx_widgets_t;
static ui_overview_fx_widgets_t s_overview_fx;
static ui_overview_perf_counter_t s_overview_wave_perf[DECK_CORE_DECK_COUNT];
static ui_position_interpolator_t s_overview_position_interp[DECK_CORE_DECK_COUNT];
static ui_overview_scheduler_t s_overview_scheduler;
#ifndef WIN32
#define UI_RGB565(r, g, b) \
    (uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | (((uint16_t)(g) & 0xFCu) << 3) | ((uint16_t)(b) >> 3))
/* "Punchy" waveform palette (2026-07-04): brighter cyan transients, true
 * white for transient tips, punchier blue/pink, stronger contrast. Index
 * meanings match ui_waveform_palette_for_sample(); kept in sync with the two
 * LVGL I8 canvas palettes (main WIN32 + mini) in ui_create_overview_deck_panel. */
static const uint16_t s_overview_wave_rgb565_palette[] = {
    UI_RGB565(0x00, 0x00, 0x00),  /* 0 background */
    UI_RGB565(0xFF, 0x2E, 0x6E),  /* 1 hot pink/red body */
    UI_RGB565(0x3A, 0x7B, 0xFF),  /* 2 blue mid energy */
    UI_RGB565(0x26, 0xE0, 0xFF),  /* 3 bright cyan transient */
    UI_RGB565(0xFF, 0xFF, 0xFF),  /* 4 white (tips / center) */
    UI_RGB565(0x38, 0xF5, 0x8C),  /* 5 green */
    UI_RGB565(0xFF, 0xB7, 0x33),  /* 6 amber */
    UI_RGB565(0xB5, 0x7C, 0xFF),  /* 7 purple quiet detail */
    UI_RGB565(0x5A, 0x5D, 0x64),  /* 8 grey */
    UI_RGB565(0xFF, 0x17, 0x44),  /* 9 red */
    UI_RGB565(0x6B, 0x3F, 0x00),  /* 10 active-loop background (dim amber) */
    /* 11..18 hot-cue colours (slot 0..7); kept in sync with the mini cue-line
     * colours in ui_overview_update_cue_markers. */
    UI_RGB565(0x00, 0xE6, 0x76),  /* 11 cue 0 green */
    UI_RGB565(0x00, 0xE5, 0xFF),  /* 12 cue 1 cyan */
    UI_RGB565(0xFF, 0xAB, 0x00),  /* 13 cue 2 amber */
    UI_RGB565(0xE0, 0x40, 0xFB),  /* 14 cue 3 magenta */
    UI_RGB565(0xFF, 0xD6, 0x00),  /* 15 cue 4 yellow */
    UI_RGB565(0xFF, 0x17, 0x44),  /* 16 cue 5 red */
    UI_RGB565(0x7C, 0x4D, 0xFF),  /* 17 cue 6 purple */
    UI_RGB565(0x29, 0x79, 0xFF),  /* 18 cue 7 blue */
};
static uint16_t *s_overview_wave_overlay_rgb565[DECK_CORE_DECK_COUNT] = { NULL };
static size_t    s_overview_wave_overlay_bytes = 0;
static ui_overview_wave_cache_t s_overview_wave_cache[DECK_CORE_DECK_COUNT];
static ui_overview_perf_counter_t s_overview_overlay_total_perf[DECK_CORE_DECK_COUNT];
static ui_overview_perf_counter_t s_overview_overlay_msync_perf[DECK_CORE_DECK_COUNT];
static ui_overview_perf_counter_t s_overview_overlay_ppa_perf[DECK_CORE_DECK_COUNT];
#endif
static uint32_t ui_overview_main_window_ms(uint8_t deck, const anlz_metadata_t *meta);

static lv_obj_t *s_overview_cue_markers[DECK_CORE_DECK_COUNT][8];
static lv_obj_t *s_overview_mini_cue_markers[DECK_CORE_DECK_COUNT][8];
static uint32_t s_overview_cue_fingerprint[DECK_CORE_DECK_COUNT];
static bool s_overview_cue_fingerprint_valid[DECK_CORE_DECK_COUNT];
static uint32_t s_overview_deck_duration_ms[DECK_CORE_DECK_COUNT];
static uint16_t s_overview_deck_bpm[DECK_CORE_DECK_COUNT];
static const anlz_metadata_t *s_overview_deck_meta[DECK_CORE_DECK_COUNT];
static const ui_deck_track_info_t *s_overview_deck_info[DECK_CORE_DECK_COUNT];
static ui_overview_waveform_source_info_t s_overview_wave_source[DECK_CORE_DECK_COUNT];
static int s_overview_active_tab = 0;
static uint8_t s_overview_prev_tab = 0xFFu;
static uint8_t s_overview_zoom_step = 2u;
#ifndef WIN32
static uint8_t s_overview_wave_load_reblit_remaining[DECK_CORE_DECK_COUNT];
#endif

#ifndef WIN32
static void ui_overview_arm_all_wave_reblits(void)
{
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        s_overview_wave_load_reblit_remaining[i] = 3u;
    }
}
#endif

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

// Tap-to-seek on the mini (full-track) waveform: jump playback to the tapped
// position mapped across the whole track (not the zoom window). Active whenever
// a track is loaded (duration > 0), so a tap while playing continues from the
// new point.
static void mini_waveform_seek_event_cb(lv_event_t *e) {
    lv_obj_t *border = lv_event_get_target(e);
    uint8_t deck = ui_event_deck(e);
    uint8_t idx = ui_overview_deck_index(deck);
    uint32_t duration_ms = s_overview_deck_duration_ms[idx];
    if (duration_ms == 0) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t content;
    lv_obj_get_content_coords(border, &content);
    int rel_x = (int)p.x - content.x1;
    if (rel_x < 0) rel_x = 0;
    if (rel_x > OVERVIEW_MINI_CV_W) rel_x = OVERVIEW_MINI_CV_W;

    int64_t target = ((int64_t)rel_x * (int64_t)duration_ms) / OVERVIEW_MINI_CV_W;
    if (target < 0) target = 0;
    if (target > (int64_t)duration_ms) target = duration_ms;
    uint32_t target_ms = (uint32_t)target;

    if (s_overview_config.actions.seek) {
        s_overview_config.actions.seek(deck, target_ms);
    }
    ESP_LOGI(TAG, "D%u mini waveform seek -> %lu ms", (unsigned)deck + 1u,
             (unsigned long)target_ms);
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
    /* Fill matches the Library LOAD DECK 1/2 buttons: accent for D1, green for D2,
     * with dark on-accent text. */
    lv_color_t bg = (idx == CTRL_DECK_1) ? COL_ACCENT : COL_GREEN;

    lv_obj_set_style_bg_color(panel->label_deck, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_deck, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(panel->label_deck, COL_ON_ACCENT, LV_PART_MAIN);

    /* The fill already carries the deck colour, so PFL / performance-target use a
     * contrasting white outline that reads on both the accent and green fills. */
    if (pfl_on) {
        lv_obj_set_style_border_color(panel->label_deck, COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_border_width(panel->label_deck, 3, LV_PART_MAIN);
    } else if (idx == ui_overview_deck_index(s_overview_performance_target)) {
        lv_obj_set_style_border_color(panel->label_deck, COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_border_width(panel->label_deck, 2, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(panel->label_deck, bg, LV_PART_MAIN);
        lv_obj_set_style_border_width(panel->label_deck, 1, LV_PART_MAIN);
    }
}

static void ui_overview_apply_play_button(ui_overview_deck_panel_t *panel, bool playing)
{
    if (!panel || !panel->play_button) {
        return;
    }

    /* Media-player convention: the button shows the action it performs. While
     * playing it is red with the pause glyph (two bars); while paused/stopped it
     * is light green with the play glyph (triangle). */
    if (panel->play_label) {
        ui_label_set_text_if_changed(panel->play_label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    lv_obj_set_style_bg_color(panel->play_button,
                              playing ? COL_RED : COL_GREEN,
                              LV_PART_MAIN);
    lv_obj_set_style_border_color(panel->play_button, playing ? COL_RED : COL_GREEN, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->play_button, 1, LV_PART_MAIN);
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
    panel->last_mini_played_w = -1;
    panel->last_playhead_x = -1;
    panel->last_vu_level = -1;
    panel->last_wave_center_ms = UINT32_MAX;
    panel->last_wave_window_ms = 0;
    panel->last_time_bucket = UINT32_MAX;
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

    panel->label_deck = ui_overview_value_label(panel->panel, &lv_font_montserrat_16,
                                                COL_TEXT,
                                                OVERVIEW_DECK_BADGE_X,
                                                top_y + OVERVIEW_DECK_BADGE_Y_OFFSET,
                                                OVERVIEW_DECK_BADGE_W,
                                                deck == CTRL_DECK_1 ? "D1" : "D2");
    lv_obj_set_size(panel->label_deck, OVERVIEW_DECK_BADGE_W, OVERVIEW_DECK_BADGE_H);
    lv_obj_set_style_bg_color(panel->label_deck, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_deck, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_align(panel->label_deck, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel->label_deck, 10, LV_PART_MAIN);
    lv_obj_set_user_data(panel->label_deck, (void *)(uintptr_t)deck);
    lv_obj_add_flag(panel->label_deck, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel->label_deck, overview_deck_select_event_cb, LV_EVENT_CLICKED, NULL);
    ui_overview_apply_deck_badge(deck);
    panel->label_status = NULL;
    /* The blue title strip is the container; the title text is its child,
     * vertically centred with LV_ALIGN_LEFT_MID so it no longer top-aligns and
     * "touches" the strip top (font-metric-robust, matches the transport
     * button pattern). The strip's own blue bg shows through the label. */
    lv_obj_t *title_strip = ui_overview_bar(panel->panel, info_x, OVERVIEW_TITLE_Y,
                                            OVERVIEW_DECK_INFO_W, OVERVIEW_TITLE_H, COL_TITLE_BLUE);
    lv_obj_clear_flag(title_strip, LV_OBJ_FLAG_SCROLLABLE);
    panel->label_title = lv_label_create(title_strip);
    lv_label_set_text(panel->label_title, "NO TRACK");
    lv_obj_set_style_text_font(panel->label_title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(panel->label_title, COL_TEXT, LV_PART_MAIN);
    lv_label_set_long_mode(panel->label_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(panel->label_title, OVERVIEW_TITLE_TEXT_W);
    lv_obj_align(panel->label_title, LV_ALIGN_LEFT_MID, 8, 0);
    panel->label_artist = ui_overview_value_label(panel->panel, &lv_font_montserrat_12,
                                                  COL_TEXT_MUTED, info_x + 8, OVERVIEW_INFO_ROW_Y, 118, "TRACK");
    lv_obj_add_flag(panel->label_artist, LV_OBJ_FLAG_HIDDEN);
    /* Time counters live on the BPM row (blue strip is title-only): elapsed at the
     * title-aligned start, remaining just to its right with a small gap, both at
     * the BPM font size. */
    panel->title_time_bg = NULL;
    panel->label_time_elapsed = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                COL_TEXT_MUTED, info_x + OVERVIEW_TIME_X,
                                                OVERVIEW_BPM_Y,
                                                OVERVIEW_ELAPSED_W, "--:--");
    lv_obj_set_style_text_align(panel->label_time_elapsed, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    panel->label_time = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                                COL_TEXT, info_x + OVERVIEW_REMAIN_X,
                                                OVERVIEW_BPM_Y,
                                                OVERVIEW_REMAIN_W, "--:--");
    lv_obj_set_style_text_align(panel->label_time, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    panel->label_bpm = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,
                                               COL_TEXT, info_x + OVERVIEW_BPM_X,
                                               OVERVIEW_BPM_Y, OVERVIEW_BPM_W, "120.00");
    lv_obj_set_style_text_align(panel->label_bpm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    panel->label_pitch = ui_overview_value_label(panel->panel, &lv_font_montserrat_18,
                                                 COL_GREEN, info_x + OVERVIEW_PITCH_X,
                                                 OVERVIEW_PITCH_Y, OVERVIEW_PITCH_W, "+0.00%");
    lv_obj_set_size(panel->label_pitch, OVERVIEW_PITCH_CHIP_W, OVERVIEW_PITCH_CHIP_H);
    lv_obj_set_style_text_align(panel->label_pitch, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel->label_pitch, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->label_pitch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel->label_pitch, COL_GREEN, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->label_pitch, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel->label_pitch, 2, LV_PART_MAIN);

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
        lv_canvas_set_palette(panel->wave_canvas, 1, lv_color32_make(0xFF, 0x2E, 0x6E, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 2, lv_color32_make(0x3A, 0x7B, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 3, lv_color32_make(0x26, 0xE0, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 4, lv_color32_make(0xFF, 0xFF, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 5, lv_color32_make(0x38, 0xF5, 0x8C, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 6, lv_color32_make(0xFF, 0xB7, 0x33, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 7, lv_color32_make(0xB5, 0x7C, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 8, lv_color32_make(0x5A, 0x5D, 0x64, 0xFF));
        lv_canvas_set_palette(panel->wave_canvas, 9, lv_color32_make(0xFF, 0x17, 0x44, 0xFF));

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
#ifndef WIN32
    /* On firmware the PPA chrome path draws its own playhead directly into
     * the framebuffer.  Keeping this LVGL object visible causes flicker
     * because LVGL redraws its white bar between PPA blits. */
    lv_obj_add_flag(panel->playhead, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_move_foreground(panel->wave_border);

    panel->mini_wave_border = lv_obj_create(panel->panel);
    lv_obj_remove_style_all(panel->mini_wave_border);
    lv_obj_set_style_bg_color(panel->mini_wave_border, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->mini_wave_border, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel->mini_wave_border, 0, LV_PART_MAIN);
    lv_obj_set_size(panel->mini_wave_border, OVERVIEW_MINI_CV_W, OVERVIEW_MINI_CV_H);
    lv_obj_set_pos(panel->mini_wave_border, info_x + 4, OVERVIEW_MINI_WAVE_Y);
    /* Tap-to-seek across the full track: keep the border clickable (its canvas,
     * played overlay, cue markers and playhead are all non-clickable, so taps
     * land here) and tag it with the deck so the handler seeks the right one. */
    lv_obj_set_user_data(panel->mini_wave_border, (void *)(uintptr_t)deck);
    lv_obj_remove_flag(panel->mini_wave_border, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel->mini_wave_border, mini_waveform_seek_event_cb, LV_EVENT_CLICKED, NULL);

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
        lv_canvas_set_palette(panel->mini_wave_canvas, 1, lv_color32_make(0xFF, 0x2E, 0x6E, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 2, lv_color32_make(0x3A, 0x7B, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 3, lv_color32_make(0x26, 0xE0, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 4, lv_color32_make(0xFF, 0xFF, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 5, lv_color32_make(0x38, 0xF5, 0x8C, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 6, lv_color32_make(0xFF, 0xB7, 0x33, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 7, lv_color32_make(0xB5, 0x7C, 0xFF, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 8, lv_color32_make(0x5A, 0x5D, 0x64, 0xFF));
        lv_canvas_set_palette(panel->mini_wave_canvas, 9, lv_color32_make(0xFF, 0x17, 0x44, 0xFF));
        lv_image_dsc_t *mini_dsc = lv_canvas_get_image(panel->mini_wave_canvas);
        if (mini_dsc && mini_dsc->header.stride > 0) {
            panel->mini_wave_stride_px = (int)mini_dsc->header.stride;
        }
    } else {
        ESP_LOGE(TAG, "D%u mini overview canvas alloc failed (%u bytes)",
                 (unsigned)deck + 1u, (unsigned)mini_sz);
    }

    /* Mini "played" highlight: a translucent overlay covering the portion of the
     * full-track overview the playhead has passed. Created before the cue lines
     * and playhead so both stay on top. Width is driven each frame in the
     * progress update. */
    panel->mini_played = lv_obj_create(panel->mini_wave_border);
    lv_obj_remove_style_all(panel->mini_played);
    lv_obj_set_style_bg_color(panel->mini_played, lv_color_hex(0xFFB05A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel->mini_played, 80, LV_PART_MAIN);
    lv_obj_set_size(panel->mini_played, 0, OVERVIEW_MINI_CV_H);
    lv_obj_set_pos(panel->mini_played, 0, 0);
    lv_obj_add_flag(panel->mini_played, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(panel->mini_played, LV_OBJ_FLAG_CLICKABLE);
    panel->last_mini_played_w = -1;

    /* Mini hot-cue lines: created before the playhead so the playhead stays on
     * top; positioned/coloured (and shown) in ui_overview_update_cue_markers. */
    for (int i = 0; i < 8; i++) {
        lv_obj_t *mc = lv_obj_create(panel->mini_wave_border);
        lv_obj_set_style_border_width(mc, 0, LV_PART_MAIN);
        lv_obj_set_size(mc, 1, OVERVIEW_MINI_CV_H);
        lv_obj_set_pos(mc, 0, 0);
        lv_obj_add_flag(mc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mc, LV_OBJ_FLAG_CLICKABLE);
        s_overview_mini_cue_markers[ui_overview_deck_index(deck)][i] = mc;
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
        lv_obj_set_size(s_beat_pulses[deck_idx][i],
                        OVERVIEW_BEAT_STRIP_DOT_SIZE_PX,
                        OVERVIEW_BEAT_STRIP_DOT_SIZE_PX);
        int pulse_y = OVERVIEW_BEAT_STRIP_TOP_Y +
            ((deck_idx == CTRL_DECK_1) ? 0 : OVERVIEW_BEAT_STRIP_ROW_GAP_PX);
        int pulse_x = OVERVIEW_WAVE_CENTER_X + ui_overview_beat_strip_offset_x(i);
        lv_obj_set_pos(s_beat_pulses[deck_idx][i], pulse_x, pulse_y);
        lv_obj_set_style_radius(s_beat_pulses[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_beat_pulses[deck_idx][i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_beat_pulses[deck_idx][i], COL_PANEL_DK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[deck_idx][i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[deck_idx][i], COL_BORDER_LT, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_beat_pulses[deck_idx][i], 1, LV_PART_MAIN);
    }

    panel->play_button = ui_overview_compact_button(panel->panel,
                                                    deck,
                                                    OVERVIEW_TRANSPORT_X,
                                                    top_y + OVERVIEW_TRANSPORT_PLAY_Y_OFFSET,
                                                    OVERVIEW_TRANSPORT_W,
                                                    "PLAY", &s_style_btn_primary,
                                                    play_pause_event_cb);
    panel->play_label = lv_obj_get_child(panel->play_button, 0);
    /* The play/pause label is a media glyph (play triangle / pause bars), sized
     * up from the default button font so it reads as an icon rather than text. */
    if (panel->play_label) {
        lv_obj_set_style_text_font(panel->play_label, &lv_font_montserrat_20, LV_PART_MAIN);
    }
    /* Seed the paused-state styling once; per-frame updates now only run on an
     * actual play/pause transition (see ui_update_overview_deck). */
    ui_overview_apply_play_button(panel, false);
    ui_overview_compact_button(panel->panel,
                               deck,
                               OVERVIEW_TRANSPORT_X,
                               top_y + OVERVIEW_TRANSPORT_CUE_Y_OFFSET,
                               OVERVIEW_TRANSPORT_W,
                               "CUE",
                               &s_style_btn_amber,
                               cue_event_cb);

    for (int i = 0; i < OVERVIEW_VU_SEGMENT_COUNT; i++) {
        int seg_index_from_top = OVERVIEW_VU_SEGMENT_COUNT - 1 - i;
        lv_obj_t *vu_segment = lv_obj_create(panel->panel);
        lv_obj_remove_style_all(vu_segment);
        lv_obj_set_size(vu_segment, OVERVIEW_VU_SEGMENT_W, OVERVIEW_VU_SEGMENT_H);
        lv_obj_set_pos(vu_segment,
                       OVERVIEW_VU_X,
                       top_y + OVERVIEW_VU_Y_OFFSET +
                           (seg_index_from_top * (OVERVIEW_VU_SEGMENT_H + OVERVIEW_VU_SEGMENT_GAP)));
        lv_obj_set_style_bg_color(vu_segment, COL_PANEL_DK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vu_segment, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_border_color(vu_segment, COL_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(vu_segment, 1, LV_PART_MAIN);
        lv_obj_remove_flag(vu_segment, LV_OBJ_FLAG_CLICKABLE);
        panel->vu_segment[i] = vu_segment;
    }
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

/* Per-effect accent colour so a glance tells you which FX is armed. FILTER and
 * ECHO reuse existing theme tokens; FLANGER gets one inline magenta (feature
 * colours stay inline, like the beat-jump reds). */
static lv_color_t ui_overview_fx_effect_color(deck_core_beat_fx_effect_t effect)
{
    switch (effect) {
    case DECK_CORE_BEAT_FX_ECHO:    return COL_AMBER;
    case DECK_CORE_BEAT_FX_FLANGER: return lv_color_hex(0xB44AE0);
    case DECK_CORE_BEAT_FX_FILTER:
    default:                        return COL_ACCENT;
    }
}

static lv_obj_t *ui_fx_target_pill(lv_obj_t *parent, int x, const char *text,
                                   lv_obj_t **out_label)
{
    lv_obj_t *pill = ui_overview_bar(parent, x, OVERVIEW_FX_PILL_Y,
                                     OVERVIEW_FX_PILL_W, OVERVIEW_FX_PILL_H, COL_ACCENT);
    lv_obj_set_style_radius(pill, OVERVIEW_FX_PILL_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pill, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = ui_fx_panel_label(parent, text, x, OVERVIEW_FX_PILL_Y + 3,
                                        OVERVIEW_FX_PILL_W, &lv_font_montserrat_14, COL_TEXT_DIM);
    if (out_label) {
        *out_label = label;
    }
    return pill;
}

static void ui_create_overview_fx_panel(lv_obj_t *parent)
{
    const int fx_x = OVERVIEW_FX_PANEL_X;
    const int fx_w = OVERVIEW_FX_PANEL_W;
    const int row_x = OVERVIEW_FX_ROW_X;
    const int row_w = OVERVIEW_FX_ROW_W;

    s_overview_fx_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_overview_fx_panel);
    lv_obj_add_style(s_overview_fx_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_overview_fx_panel, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overview_fx_panel, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overview_fx_panel, 1, LV_PART_MAIN);
    lv_obj_set_size(s_overview_fx_panel, fx_w, OVERVIEW_FX_PANEL_H);
    lv_obj_set_pos(s_overview_fx_panel, fx_x, OVERVIEW_FX_PANEL_Y);
    lv_obj_clear_flag(s_overview_fx_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_overview_fx.header = ui_overview_bar(s_overview_fx_panel, 0, 0, fx_w, 24, COL_PANEL);
    lv_obj_remove_flag(s_overview_fx.header, LV_OBJ_FLAG_CLICKABLE);
    s_overview_fx.header_label = ui_fx_panel_label(s_overview_fx_panel, "FX", 0, 5, fx_w,
                                                   &lv_font_montserrat_12, COL_TEXT);

    /* Effect identity chip — filled in the effect colour when the FX is live. */
    s_overview_fx.effect_chip = ui_overview_bar(s_overview_fx_panel,
                                                OVERVIEW_FX_CHIP_X, OVERVIEW_FX_CHIP_Y,
                                                OVERVIEW_FX_CHIP_W, OVERVIEW_FX_CHIP_H, COL_ACCENT);
    lv_obj_set_style_radius(s_overview_fx.effect_chip, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overview_fx.effect_chip, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overview_fx.effect_chip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overview_fx.effect_chip, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_remove_flag(s_overview_fx.effect_chip, LV_OBJ_FLAG_CLICKABLE);
    s_overview_fx.effect = ui_fx_panel_label(s_overview_fx_panel, "FILTER",
                                             row_x, OVERVIEW_FX_CHIP_Y + 8, row_w,
                                             &lv_font_montserrat_14, COL_ACCENT);

    /* Target channel pills. */
    ui_fx_panel_label(s_overview_fx_panel, "TARGET", row_x, OVERVIEW_FX_TARGET_CAPTION_Y, row_w,
                      &lv_font_montserrat_12, COL_TEXT_DIM);
    s_overview_fx.pill_bg[0] = ui_fx_target_pill(s_overview_fx_panel, OVERVIEW_FX_PILL1_X, "1",
                                                 &s_overview_fx.pill_label[0]);
    s_overview_fx.pill_bg[1] = ui_fx_target_pill(s_overview_fx_panel, OVERVIEW_FX_PILL2_X, "2",
                                                 &s_overview_fx.pill_label[1]);

    /* Beat-division chip. */
    ui_fx_panel_label(s_overview_fx_panel, "BEAT", row_x, OVERVIEW_FX_BEAT_CAPTION_Y, row_w,
                      &lv_font_montserrat_12, COL_TEXT_DIM);
    s_overview_fx.beat_chip = ui_overview_bar(s_overview_fx_panel,
                                              OVERVIEW_FX_BEAT_CHIP_X, OVERVIEW_FX_BEAT_CHIP_Y,
                                              OVERVIEW_FX_BEAT_CHIP_W, OVERVIEW_FX_BEAT_CHIP_H,
                                              COL_PANEL_DK);
    lv_obj_set_style_radius(s_overview_fx.beat_chip, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overview_fx.beat_chip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overview_fx.beat_chip, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_remove_flag(s_overview_fx.beat_chip, LV_OBJ_FLAG_CLICKABLE);
    s_overview_fx.beat = ui_fx_panel_label(s_overview_fx_panel, "1",
                                           OVERVIEW_FX_BEAT_CHIP_X, OVERVIEW_FX_BEAT_CHIP_Y + 4,
                                           OVERVIEW_FX_BEAT_CHIP_W, &lv_font_montserrat_14, COL_TEXT);

    /* Depth vertical fill meter — the live-ride element. */
    ui_fx_panel_label(s_overview_fx_panel, "DEPTH", row_x, OVERVIEW_FX_DEPTH_CAPTION_Y, row_w,
                      &lv_font_montserrat_12, COL_TEXT_DIM);
    s_overview_fx.depth_bg = ui_overview_bar(s_overview_fx_panel,
                                             OVERVIEW_FX_DEPTH_BAR_X, OVERVIEW_FX_DEPTH_BAR_Y,
                                             OVERVIEW_FX_DEPTH_BAR_W, OVERVIEW_FX_DEPTH_BAR_H, COL_BG);
    lv_obj_set_style_radius(s_overview_fx.depth_bg, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overview_fx.depth_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overview_fx.depth_bg, COL_BORDER, LV_PART_MAIN);
    lv_obj_remove_flag(s_overview_fx.depth_bg, LV_OBJ_FLAG_CLICKABLE);
    s_overview_fx.depth_fill = ui_overview_bar(s_overview_fx_panel,
                                               OVERVIEW_FX_DEPTH_BAR_X,
                                               OVERVIEW_FX_DEPTH_BAR_Y + OVERVIEW_FX_DEPTH_BAR_H / 2,
                                               OVERVIEW_FX_DEPTH_BAR_W,
                                               OVERVIEW_FX_DEPTH_BAR_H / 2, COL_ACCENT);
    lv_obj_set_style_radius(s_overview_fx.depth_fill, 4, LV_PART_MAIN);
    lv_obj_remove_flag(s_overview_fx.depth_fill, LV_OBJ_FLAG_CLICKABLE);
    s_overview_fx.depth = ui_fx_panel_label(s_overview_fx_panel, "50%",
                                            row_x, OVERVIEW_FX_DEPTH_VALUE_Y, row_w,
                                            &lv_font_montserrat_16, COL_TEXT);
}

static void ui_update_overview_fx_panel(const deck_core_beat_fx_state_t *state)
{
    if (!s_overview_fx_panel) {
        return;
    }

    ui_beat_fx_overview_text_t text = {0};
    ui_beat_fx_format_overview(state, &text);

    const bool on = state && state->enabled;
    const deck_core_beat_fx_effect_t effect = state ? state->effect : DECK_CORE_BEAT_FX_FILTER;
    const lv_color_t ec = ui_overview_fx_effect_color(effect);

    ui_label_set_text_if_changed(s_overview_fx.effect, text.effect);
    ui_label_set_text_if_changed(s_overview_fx.beat, text.beat);
    ui_label_set_text_if_changed(s_overview_fx.depth, text.depth);

    /* Panel border + header carry the on/off + effect-colour signal. */
    ui_obj_set_border_color_if_changed(s_overview_fx_panel, on ? ec : COL_BORDER);
    ui_obj_set_bg_color_if_changed(s_overview_fx.header, on ? ec : COL_PANEL);
    ui_obj_set_text_color_if_changed(s_overview_fx.header_label, on ? COL_ON_ACCENT : COL_TEXT_MUTED);

    /* Effect chip: filled + dark text when on, outlined + dim when off. */
    ui_obj_set_bg_color_if_changed(s_overview_fx.effect_chip, ec);
    ui_obj_set_bg_opa_if_changed(s_overview_fx.effect_chip, on ? LV_OPA_COVER : LV_OPA_TRANSP);
    ui_obj_set_border_color_if_changed(s_overview_fx.effect_chip, on ? ec : COL_BORDER_LT);
    ui_obj_set_text_color_if_changed(s_overview_fx.effect, on ? COL_ON_ACCENT : COL_TEXT_DIM);

    /* Target pills: light the routed channel(s) in the effect colour. */
    const bool ch_on[2] = {
        on && (state->target == CTRL_BEAT_FX_TARGET_CH1 || state->target == CTRL_BEAT_FX_TARGET_BOTH),
        on && (state->target == CTRL_BEAT_FX_TARGET_CH2 || state->target == CTRL_BEAT_FX_TARGET_BOTH),
    };
    for (int i = 0; i < 2; i++) {
        ui_obj_set_bg_color_if_changed(s_overview_fx.pill_bg[i], ec);
        ui_obj_set_bg_opa_if_changed(s_overview_fx.pill_bg[i], ch_on[i] ? LV_OPA_COVER : LV_OPA_TRANSP);
        ui_obj_set_border_color_if_changed(s_overview_fx.pill_bg[i], ch_on[i] ? ec : COL_BORDER_LT);
        ui_obj_set_text_color_if_changed(s_overview_fx.pill_label[i], ch_on[i] ? COL_ON_ACCENT : COL_TEXT_DIM);
    }

    /* Beat chip. */
    ui_obj_set_border_color_if_changed(s_overview_fx.beat_chip, on ? ec : COL_BORDER_LT);
    ui_obj_set_text_color_if_changed(s_overview_fx.beat, on ? COL_TEXT : COL_TEXT_DIM);

    /* Depth vertical fill (grows bottom-up). */
    unsigned depth = state ? state->depth : 0u;
    if (depth > 127u) {
        depth = 127u;
    }
    int fill_h = on ? (int)((OVERVIEW_FX_DEPTH_BAR_H * depth + 63u) / 127u) : 0;
    if (fill_h < 0) {
        fill_h = 0;
    }
    int fill_y = OVERVIEW_FX_DEPTH_BAR_Y + (OVERVIEW_FX_DEPTH_BAR_H - fill_h);
    if (lv_obj_get_height(s_overview_fx.depth_fill) != fill_h) {
        lv_obj_set_height(s_overview_fx.depth_fill, fill_h);
    }
    if (lv_obj_get_y(s_overview_fx.depth_fill) != fill_y) {
        lv_obj_set_y(s_overview_fx.depth_fill, fill_y);
    }
    ui_obj_set_bg_color_if_changed(s_overview_fx.depth_fill, ec);
    ui_obj_set_bg_opa_if_changed(s_overview_fx.depth_fill,
                                 (on && fill_h > 0) ? LV_OPA_COVER : LV_OPA_TRANSP);
    ui_obj_set_text_color_if_changed(s_overview_fx.depth, on ? COL_TEXT : COL_TEXT_DIM);
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
    ui_create_overview_fx_panel(screen);
    return screen;
}


// ─── Waveform Helpers ────────────────────────────────────────────────────────

static uint32_t ui_overview_main_window_ms(uint8_t deck, const anlz_metadata_t *meta)
{
    uint16_t bpm_x100 = 0;
    if (meta && meta->beats && meta->beat_count > 0 && meta->beats[0].bpm_x100 > 0) {
        bpm_x100 = meta->beats[0].bpm_x100;
    }
    return ui_overview_window_ms_from_bpm_x100_for_zoom(
        bpm_x100,
        s_overview_deck_bpm[ui_overview_deck_index(deck)],
        s_overview_zoom_step);
}

esp_err_t ui_overview_zoom_delta(int delta)
{
    if (delta == 0) {
        return ESP_OK;
    }

    uint8_t next = ui_overview_zoom_apply_delta(s_overview_zoom_step, delta);
    if (next == s_overview_zoom_step) {
        return ESP_OK;
    }

    s_overview_zoom_step = next;
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        s_overview_decks[i].last_wave_center_ms = UINT32_MAX;
        s_overview_decks[i].last_wave_window_ms = 0;
    }

    ESP_LOGI(TAG,
             "overview waveform zoom: %u beats",
             (unsigned)ui_overview_zoom_visible_beats_for_step(s_overview_zoom_step));
    return ESP_OK;
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
        /* Downbeat triangle on each deck's INNER edge (deck 1 top wave -> bottom,
         * deck 2 bottom wave -> top) so the two decks' markers meet base-to-base
         * at the shared boundary between the waveforms. */
        ui_overview_wave_cache_set_regular_beat_cap_bottom(&s_overview_wave_cache[idx],
                                                           idx == 0);
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
    ui_overview_wave_cache_set_regular_beat_cap_bottom(&s_overview_wave_cache[idx],
                                                       idx == 0);
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

/* ---------- playhead burn-in helpers ---------- */

/* Maximum playhead width in pixels – must match OVERVIEW_PLAYHEAD_W. */
#define PLAYHEAD_BURN_MAX_W 4

/* Temporary column storage for save / restore around the PPA blit.
 * Each column is OVERVIEW_CV_H uint16_t values.  We need at most
 * PLAYHEAD_BURN_MAX_W columns, but the ring wrap may split them into two
 * runs, so we keep room for the worst case (all columns). */
typedef struct {
    uint16_t saved[PLAYHEAD_BURN_MAX_W][OVERVIEW_CV_H];
    int      physical[PLAYHEAD_BURN_MAX_W];   /* physical x in strip */
    int      count;
} playhead_burn_ctx_t;

static void playhead_burn_save_and_fill(ui_overview_wave_cache_t *cache,
                                        playhead_burn_ctx_t *ctx,
                                        uint16_t color)
{
    ctx->count = 0;
    if (!cache || !cache->pixels || cache->strip_width_px <= 0 ||
        cache->view_width_px <= 0 || cache->height_px <= 0) {
        return;
    }

    int center_logical = cache->view_origin_px + cache->view_width_px / 2;
    int half = OVERVIEW_PLAYHEAD_W / 2;   /* 1 for a 3-px playhead */

    for (int dx = -half; dx <= half && ctx->count < PLAYHEAD_BURN_MAX_W; dx++) {
        int logical_x = center_logical + dx;
        int px = (cache->ring_head_px + logical_x) % cache->strip_width_px;
        if (px < 0) px += cache->strip_width_px;
        if (px >= cache->strip_width_px) continue;

        ctx->physical[ctx->count] = px;
        /* save the original column */
        for (int y = 0; y < cache->height_px; y++) {
            ctx->saved[ctx->count][y] = cache->pixels[y * cache->stride_px + px];
        }
        /* overwrite with playhead colour */
        for (int y = 0; y < cache->height_px; y++) {
            cache->pixels[y * cache->stride_px + px] = color;
        }
        ctx->count++;
    }
}

static void playhead_burn_restore(ui_overview_wave_cache_t *cache,
                                  const playhead_burn_ctx_t *ctx)
{
    if (!cache || !cache->pixels || !ctx) return;
    for (int i = 0; i < ctx->count; i++) {
        int px = ctx->physical[i];
        for (int y = 0; y < cache->height_px; y++) {
            cache->pixels[y * cache->stride_px + px] = ctx->saved[i][y];
        }
    }
}

/* ---------- overlay blit ---------- */

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

    /* Burn the playhead into the strip so the PPA blit transfers it
     * atomically with the waveform – no separate framebuffer write. */
    const uint16_t playhead_color = UI_RGB565(0x00, 0xFF, 0x00);
    playhead_burn_ctx_t burn_ctx;
    playhead_burn_save_and_fill(&s_overview_wave_cache[idx], &burn_ctx,
                                playhead_color);

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
            playhead_burn_restore(&s_overview_wave_cache[idx], &burn_ctx);
            return false;
        }
    }

    /* Restore the strip so the cache stays clean for future scrolling. */
    playhead_burn_restore(&s_overview_wave_cache[idx], &burn_ctx);

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
                                             uint32_t window_ms,
                                             bool loop_active,
                                             uint32_t loop_start_ms,
                                             uint32_t loop_end_ms)
{
    if (!panel) {
        return;
    }
    bool main_wave_rendered = false;

#ifndef WIN32
    uint8_t idx = ui_overview_deck_index(deck);
    if (s_overview_active_tab != 0) {
        return;
    }
    if (idx < DECK_CORE_DECK_COUNT &&
        ui_overview_scheduler_direct_overlay_allowed(idx) &&
        ui_overview_wave_overlay_ensure_buffer(idx)) {
        uint16_t *overlay = s_overview_wave_overlay_rgb565[idx];
        ui_overview_wave_cache_report_t cache_report = {0};
        /* Feed the resolved loop region (active loop, or an armed loop-in growing
         * to the playhead) so the cache tints it; a change flips it invalid. */
        ui_overview_wave_cache_set_loop(&s_overview_wave_cache[idx], loop_active,
                                        loop_start_ms, loop_end_ms);
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
        main_wave_rendered = true;
        if (idx < DECK_CORE_DECK_COUNT && s_overview_wave_load_reblit_remaining[idx] > 0) {
            s_overview_wave_load_reblit_remaining[idx]--;
        }
        if (ui_diagnostics_enabled()) {
            ui_overview_perf_report_t report;
            if (ui_overview_perf_record(&s_overview_wave_perf[idx],
                                        cache_us,
                                        &report)) {
                ui_overview_wave_cache_stats_t stats = {0};
                ui_overview_wave_cache_get_stats(&s_overview_wave_cache[idx], &stats);
                ESP_LOGI(TAG,
                         "D%u overview main cache: kind=%u dx=%d cols=%u blits=%u totals full=%u offset=%u edge=%u none=%u cols=%u blits=%u cache_last=%u us cache_avg=%u us cache_max=%u us ppa_us=%u samples=%u",
                         (unsigned)(idx + 1u),
                         (unsigned)cache_report.kind,
                         cache_report.scroll_dx_px,
                         (unsigned)cache_report.columns_rendered,
                         (unsigned)cache_report.blit_count,
                         (unsigned)stats.update_count[UI_OVERVIEW_WAVE_CACHE_FULL],
                         (unsigned)stats.update_count[UI_OVERVIEW_WAVE_CACHE_OFFSET],
                         (unsigned)stats.update_count[UI_OVERVIEW_WAVE_CACHE_EDGE],
                         (unsigned)stats.update_count[UI_OVERVIEW_WAVE_CACHE_NONE],
                         (unsigned)stats.total_columns_rendered,
                         (unsigned)stats.total_blits,
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
    (void)loop_active;
    (void)loop_start_ms;
    (void)loop_end_ms;
    if (!panel->wave_buf) {
        return;
    }
    const int W = OVERVIEW_CV_W;
    const int H = OVERVIEW_CV_H;
    uint8_t *buf = panel->wave_buf + 256 * sizeof(lv_color32_t);
    const int S = panel->wave_stride_px;
    ui_overview_renderer_draw_main_with_options(buf, S, W, H, source,
                                                duration_ms, meta,
                                                center_ms, window_ms,
                                                ui_overview_deck_index(deck) == 0);
    lv_obj_invalidate(panel->wave_canvas);
    main_wave_rendered = true;
#endif
    if (!main_wave_rendered) {
        return;
    }
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
    s_overview_cue_fingerprint_valid[idx] = false;
    panel->last_mini_fill_x = -1;
    panel->last_mini_played_w = -1;
    if (panel->mini_played) {
        lv_obj_add_flag(panel->mini_played, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(panel->mini_played, 0, OVERVIEW_MINI_CV_H);
    }
    panel->last_playhead_x = -1;
    panel->last_wave_center_ms = UINT32_MAX;
    panel->last_wave_window_ms = 0;
    panel->last_time_bucket = UINT32_MAX;
#ifndef WIN32
    ui_overview_wave_cache_reset(&s_overview_wave_cache[idx]);
    ui_overview_arm_all_wave_reblits();
#endif
    ui_waveform_source_t wave_source =
        ui_waveform_source_select(meta, waveform_low, has_waveform);
    bool wave_valid = wave_source.kind != UI_WAVEFORM_SOURCE_NONE && duration_ms > 0;

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

/* FNV-1a over the cue layout + track duration. Lets the 1 Hz slow-update skip
 * the expensive strip re-render + reblit unless the cues actually changed. */
static uint32_t ui_overview_cue_fingerprint(const anlz_metadata_t *meta, uint32_t duration_ms)
{
    uint32_t fp = 2166136261u;
    fp = (fp ^ duration_ms) * 16777619u;
    if (meta) {
        fp = (fp ^ (uint32_t)meta->cue_count) * 16777619u;
        for (uint8_t j = 0; j < meta->cue_count && j < ANLZ_MAX_CUES; j++) {
            fp = (fp ^ (uint32_t)meta->cues[j].index) * 16777619u;
            fp = (fp ^ meta->cues[j].start_ms) * 16777619u;
        }
    }
    return fp;
}

void ui_overview_update_cue_markers(uint8_t deck, const anlz_metadata_t *meta, uint32_t duration_ms)
{
    uint8_t deck_idx = ui_overview_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[deck_idx];

    /* Skip when nothing about the cues changed. Previously this ran every second
     * and unconditionally reset the wave cache (forcing a full strip rebuild on
     * both decks ~once per second), which is the main source of the periodic
     * waveform hitch. */
    uint32_t fingerprint = ui_overview_cue_fingerprint(meta, duration_ms);
    if (s_overview_cue_fingerprint_valid[deck_idx] &&
        s_overview_cue_fingerprint[deck_idx] == fingerprint) {
        return;
    }
    s_overview_cue_fingerprint[deck_idx] = fingerprint;
    s_overview_cue_fingerprint_valid[deck_idx] = true;

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

    /* The main (zoom) waveform cue markers are now baked into the scrolling
     * strip (drawn from meta->cues by the renderer), so hide the legacy LVGL
     * marker/head objects that used to flicker over the PPA overlay. */
    for (int i = 0; i < 8; i++) {
        if (s_overview_cue_markers[deck_idx][i]) {
            lv_obj_add_flag(s_overview_cue_markers[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_overview_cue_heads[deck_idx][i]) {
            lv_obj_add_flag(s_overview_cue_heads[deck_idx][i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Mini (full-track overview) cue lines: fixed x across the whole track. */
    for (int i = 0; i < 8; i++) {
        lv_obj_t *mc = s_overview_mini_cue_markers[deck_idx][i];
        if (!mc) {
            continue;
        }
        bool found = false;
        uint32_t pos = 0;
        if (meta && duration_ms > 0) {
            for (int j = 0; j < meta->cue_count; j++) {
                if (meta->cues[j].index == i) {
                    pos = meta->cues[j].start_ms;
                    found = true;
                    break;
                }
            }
        }
        if (!found || pos > duration_ms) {
            lv_obj_add_flag(mc, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int mini_x = (int)(((int64_t)pos * OVERVIEW_MINI_CV_W) / (int64_t)duration_ms);
        if (mini_x < 0) mini_x = 0;
        if (mini_x > OVERVIEW_MINI_CV_W - 1) mini_x = OVERVIEW_MINI_CV_W - 1;
        lv_obj_set_pos(mc, mini_x, 0);
        lv_obj_set_style_bg_color(mc, lv_color_hex(cue_hex_colors[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(mc, LV_OBJ_FLAG_HIDDEN);
    }

    /* Force the main strip to re-render so newly-loaded cues appear immediately
     * (the source/window/meta key may be unchanged when cues arrive). */
#ifndef WIN32
    ui_overview_wave_cache_reset(&s_overview_wave_cache[deck_idx]);
    ui_overview_arm_all_wave_reblits();
    panel->last_wave_center_ms = UINT32_MAX;
    panel->last_wave_window_ms = 0;
#else
    (void)panel;
#endif
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
        lv_color_t color = downbeat ? lv_color_hex(0xFF1744)
                                    : (deck_idx == CTRL_DECK_1 ? COL_ACCENT : COL_GREEN);
        lv_obj_set_style_bg_color(s_beat_pulses[deck_idx][i], color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[deck_idx][i], opa, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[deck_idx][i], color, LV_PART_MAIN);
    }
}

static void ui_update_overview_beat_strip(uint8_t deck, uint32_t position_ms)
{
    uint8_t idx = ui_overview_deck_index(deck);
    ui_beat_indicator_state_t beat_state = {0};
    bool beat_valid = false;
    if (s_overview_deck_duration_ms[idx] > 0) {
        const anlz_metadata_t *meta = s_overview_deck_meta[idx];
        beat_state = ui_beat_indicator_calculate(position_ms,
                                                 meta ? meta->beats : NULL,
                                                 meta ? meta->beat_count : 0,
                                                 s_overview_deck_bpm[idx]);
        beat_valid = beat_state.valid;
    }
    ui_update_beat_indicator(idx, beat_valid ? &beat_state : NULL);
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

    uint8_t idx = ui_overview_deck_index(deck);
    const anlz_metadata_t *meta = s_overview_deck_meta[idx];
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
#ifndef WIN32
    if (idx < DECK_CORE_DECK_COUNT &&
        s_overview_wave_load_reblit_remaining[idx] > 0 &&
        source.kind != UI_WAVEFORM_SOURCE_NONE &&
        ui_overview_main_wave_ready(panel)) {
        ui_overview_wave_cache_reset(&s_overview_wave_cache[idx]);
        panel->last_wave_center_ms = UINT32_MAX;
        panel->last_wave_window_ms = 0;
        redraw_main = true;
    }
#endif

    /* Resolve the loop region to highlight: a full active loop [start,end], or an
     * armed loop-in growing from its marker to the live playhead. Firmware only;
     * the WIN32/PC main-waveform path does not draw the highlight. A change forces
     * a redraw even when the playhead is parked (compared against the cache's
     * applied loop so a scheduler veto just retries next frame). */
    bool loop_active = false;
    uint32_t loop_start_ms = 0;
    uint32_t loop_end_ms = 0;
#ifndef WIN32
    {
        deck_core_loop_display_t loop = deck_core_get_loop_display(deck);
        if (loop.active) {
            loop_active = true;
            loop_start_ms = loop.start_ms;
            loop_end_ms = loop.end_ms;
        } else if (loop.armed && position_ms > loop.start_ms) {
            loop_active = true;
            loop_start_ms = loop.start_ms;
            loop_end_ms = position_ms;
        }
        if (idx < DECK_CORE_DECK_COUNT &&
            (s_overview_wave_cache[idx].loop_active != loop_active ||
             s_overview_wave_cache[idx].loop_start_ms != loop_start_ms ||
             s_overview_wave_cache[idx].loop_end_ms != loop_end_ms)) {
            redraw_main = true;
        }
    }
#endif

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
                                         center_ms, window_ms,
                                         loop_active, loop_start_ms, loop_end_ms);
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

    /* Translucent "played" highlight over [0, playhead] on the full-track mini
     * overview. Uniform over the multi-colour waveform (unlike the old per-pixel
     * recolour) and drawn under the cue lines + playhead. */
    if (panel->mini_played && mini_x != panel->last_mini_played_w) {
        if (mini_x > 0) {
            lv_obj_set_size(panel->mini_played, mini_x, OVERVIEW_MINI_CV_H);
            lv_obj_remove_flag(panel->mini_played, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(panel->mini_played, LV_OBJ_FLAG_HIDDEN);
        }
        panel->last_mini_played_w = mini_x;
    }
}

static void ui_overview_update_vu_meter(uint8_t deck, uint16_t peak)
{
    uint8_t idx = ui_overview_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    if (!panel->panel) {
        return;
    }

    int level = (int)(((uint32_t)peak * OVERVIEW_VU_SEGMENT_COUNT) / 32768u);
    if (peak > 0 && level < 1) {
        level = 1;
    }
    if (level > OVERVIEW_VU_SEGMENT_COUNT) {
        level = OVERVIEW_VU_SEGMENT_COUNT;
    }
    int old_level = panel->last_vu_level;
    if (old_level >= 0 && level < old_level) {
        level = old_level - 1;
    }
    if (level == old_level) {
        return;
    }
    panel->last_vu_level = level;

    /* Only restyle the segments whose active state actually flips this update.
     * A typical ±1 level change touches a single segment instead of all of them, which
     * keeps the LVGL invalidate buffer from filling up (and spilling to a
     * full-screen redraw that would erase the PPA-blitted waveforms). */
    int from = 0;
    int to = OVERVIEW_VU_SEGMENT_COUNT;
    if (old_level >= 0) {
        from = level < old_level ? level : old_level;
        to = level > old_level ? level : old_level;
    }
    for (int i = from; i < to; i++) {
        lv_obj_t *vu_segment = panel->vu_segment[i];
        if (!vu_segment) {
            continue;
        }
        bool active = i < level;
        lv_color_t color = COL_PANEL_DK;
        lv_opa_t opa = LV_OPA_50;
        if (active) {
            if (i >= OVERVIEW_VU_SEGMENT_COUNT - 1) {
                color = lv_color_hex(0xFF1744);
            } else if (i >= OVERVIEW_VU_SEGMENT_COUNT - 3) {
                color = COL_AMBER;
            } else {
                color = COL_GREEN;
            }
            opa = LV_OPA_COVER;
        }
        lv_obj_set_style_bg_color(vu_segment, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vu_segment, opa, LV_PART_MAIN);
        lv_obj_set_style_border_color(vu_segment, active ? color : COL_BORDER, LV_PART_MAIN);
    }
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

static int32_t ui_overview_pitch_centipercent(const deck_state_t *state)
{
    if (!state) {
        return 0;
    }
#ifndef WIN32
    return state->pitch_centipercent;
#else
    if (state->pitch_centipercent != 0) {
        return state->pitch_centipercent;
    }
    int32_t raw = state->pitch;
    if (raw < 0) raw = 0;
    if (raw > 16383) raw = 16383;
    return ((8192 - raw) * 1000) / 8192;
#endif
}

static uint32_t ui_overview_base_bpm_x100(uint8_t idx)
{
    idx = ui_overview_deck_index(idx);
    const anlz_metadata_t *meta = s_overview_deck_meta[idx];
    if (meta && meta->beats && meta->beat_count > 0 && meta->beats[0].bpm_x100 > 0) {
        return meta->beats[0].bpm_x100;
    }
    if (s_overview_deck_bpm[idx] > 0) {
        return (uint32_t)s_overview_deck_bpm[idx] * 100u;
    }
    return 12000u;
}

static uint32_t ui_pitch_speed_permille(const deck_state_t *state)
{
    if (!state) {
        return 1000u;
    }

    float pitch_pct;
#ifndef WIN32
    pitch_pct = deck_core_pitch_percent(state);
#else
    pitch_pct = ((8192.0f - (float)state->pitch) / 8192.0f) * 10.0f;
#endif
    int speed = 1000 + (int)(pitch_pct * 10.0f + (pitch_pct >= 0.0f ? 0.5f : -0.5f));
    if (speed < 1) {
        speed = 1;
    }
    return (uint32_t)speed;
}

static void ui_overview_format_elapsed_time(char *out,
                                            size_t out_size,
                                            uint32_t duration_ms,
                                            uint32_t elapsed_ms)
{
    if (!out || out_size == 0) {
        return;
    }
    if (duration_ms == 0) {
        snprintf(out, out_size, "--:--");
        return;
    }

    uint32_t total_secs = elapsed_ms / 1000u;
    uint32_t hrs = total_secs / 3600u;
    uint32_t mins = (total_secs % 3600u) / 60u;
    uint32_t secs = total_secs % 60u;

    if (hrs > 0) {
        snprintf(out, out_size, "%u:%02u:%02u",
                 (unsigned)hrs,
                 (unsigned)mins,
                 (unsigned)secs);
    } else {
        snprintf(out, out_size, "%02u:%02u",
                 (unsigned)mins,
                 (unsigned)secs);
    }
}

static void ui_overview_format_remaining_time(char *out,
                                              size_t out_size,
                                              uint32_t duration_ms,
                                              uint32_t remain_ms)
{
    if (!out || out_size == 0) {
        return;
    }
    if (duration_ms == 0) {
        snprintf(out, out_size, "--:--");
        return;
    }

    uint32_t total_secs = remain_ms / 1000u;
    uint32_t hrs = total_secs / 3600u;
    uint32_t mins = (total_secs % 3600u) / 60u;
    uint32_t secs = total_secs % 60u;

    if (hrs > 0) {
        snprintf(out, out_size, "-%u:%02u:%02u",
                 (unsigned)hrs,
                 (unsigned)mins,
                 (unsigned)secs);
    } else {
        snprintf(out, out_size, "-%02u:%02u",
                 (unsigned)mins,
                 (unsigned)secs);
    }
}

static void ui_update_overview_deck(uint8_t deck, const deck_state_t *state,
                                    uint16_t effective_speed_permille)
{
    uint8_t idx = ui_overview_deck_index(deck);
    ui_overview_deck_panel_t *panel = &s_overview_decks[idx];
    if (!panel->panel || !state) return;

    uint32_t duration_ms = s_overview_deck_duration_ms[idx];
    /* Prefer the audio engine's effective speed (pitch fader × jog bend) so the
     * waveform tracks a jog nudge instead of lagging at the fader speed; fall
     * back to the fader-only estimate if the snapshot did not provide it. */
    uint32_t speed_permille = effective_speed_permille != 0
                            ? effective_speed_permille
                            : ui_pitch_speed_permille(state);
    uint32_t elapsed_ms = ui_position_interpolator_update(
        &s_overview_position_interp[idx],
        state->position_ms,
        duration_ms,
        state->playing,
        speed_permille,
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
        /* Only restyle the transport button on an actual play/pause transition;
         * doing it every frame re-invalidated both buttons on every refresh. */
        ui_overview_apply_play_button(panel, state->playing);
    }
    ui_label_set_text_if_changed(panel->label_title, info->valid ? info->title : "NO TRACK");

    uint32_t time_bucket = duration_ms > 0 ? (elapsed_ms / 1000u) : UINT32_MAX - 1u;
    if (time_bucket != panel->last_time_bucket) {
        char time_text[16];
        panel->last_time_bucket = time_bucket;
        ui_overview_format_elapsed_time(time_text, sizeof(time_text), duration_ms, elapsed_ms);
        ui_label_set_text_if_changed(panel->label_time_elapsed, time_text);
        ui_overview_format_remaining_time(time_text, sizeof(time_text), duration_ms, remain_ms);
        ui_label_set_text_if_changed(panel->label_time, time_text);
    }

    int32_t pitch_centipct = ui_overview_pitch_centipercent(state);
    uint32_t base_bpm_x100 = ui_overview_base_bpm_x100(idx);
    int32_t speed_centipct = 10000 + pitch_centipct;
    if (speed_centipct < 1) {
        speed_centipct = 1;
    }
    uint32_t bpm_centi = (uint32_t)(((uint64_t)base_bpm_x100 *
                                     (uint64_t)speed_centipct + 5000u) / 10000u);
    char bpm_text[12];
    snprintf(bpm_text, sizeof(bpm_text), "%u.%02u",
             (unsigned)(bpm_centi / 100u),
             (unsigned)(bpm_centi % 100u));
    ui_label_set_text_if_changed(panel->label_bpm, bpm_text);
    char pitch_text[16];
    int pitch_abs = (int)(pitch_centipct < 0 ? -pitch_centipct : pitch_centipct);
    snprintf(pitch_text, sizeof(pitch_text), "%c%d.%02d%%",
             (pitch_centipct < 0) ? '-' : '+',
             pitch_abs / 100,
             pitch_abs % 100);
    ui_label_set_text_if_changed(panel->label_pitch, pitch_text);
    /* Negative pitch (slowed down) reads red; positive/zero stays green. The green
     * chip border is left unchanged. */
    ui_obj_set_text_color_if_changed(panel->label_pitch,
                                     pitch_centipct < 0 ? COL_RED : COL_GREEN);

    ui_update_overview_waveform_progress(deck, panel, elapsed_ms, duration_ms,
                                         state->playing);
    ui_update_overview_beat_strip(deck, elapsed_ms);
}


void ui_overview_set_performance_target(uint8_t active_deck)
{
    s_overview_performance_target = ui_overview_deck_index(active_deck);
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        ui_overview_deck_panel_t *panel = &s_overview_decks[deck];
        if (panel->panel) {
            ui_overview_apply_deck_badge(deck);
        }
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

#ifndef WIN32
    /* Returning to Overview from another tab: LVGL repainted the previously
     * hidden screen and erased the direct-PPA waveforms. Re-arm the strip reblit
     * so both decks are restored even while paused (center unchanged would
     * otherwise skip the redraw). The old unconditional 1 Hz cue-marker reset
     * used to mask this by rebuilding the strip every second. */
    if (ctx->active_tab == 0 && s_overview_prev_tab != 0) {
        ui_overview_arm_all_wave_reblits();
        for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
            s_overview_decks[i].last_wave_center_ms = UINT32_MAX;
            s_overview_decks[i].last_wave_window_ms = 0;
        }
    }
#endif
    s_overview_prev_tab = (uint8_t)ctx->active_tab;

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

    ui_update_overview_deck(first_deck, &ctx->deck_state[first_deck],
                            ctx->mixer_snapshot.effective_speed_permille[first_deck]);
    ui_update_overview_deck(second_deck, &ctx->deck_state[second_deck],
                            ctx->mixer_snapshot.effective_speed_permille[second_deck]);
    ui_update_overview_fx_panel(&ctx->beat_fx_state);
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        ui_overview_update_vu_meter(deck, ctx->mixer_snapshot.deck_peak_display[deck]);
    }

    if (ctx->overview_slow_update) {
#ifndef WIN32
        ui_update_mixer_overview(&ctx->mixer_snapshot);
#endif
        ui_overview_update_cue_markers(CTRL_DECK_1,
                                       ctx->deck_meta[CTRL_DECK_1],
                                       ctx->deck_duration_ms[CTRL_DECK_1]);
        ui_overview_update_cue_markers(CTRL_DECK_2,
                                       ctx->deck_meta[CTRL_DECK_2],
                                       ctx->deck_duration_ms[CTRL_DECK_2]);
    }
}
