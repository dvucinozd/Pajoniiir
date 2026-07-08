#include "ui_performance_tabs.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

uint32_t ui_performance_tabs_calculate_jump_target(uint32_t position_ms,
                                                   uint16_t bpm,
                                                   int beat_shift,
                                                   const uint32_t *beat_times_ms,
                                                   int beat_count)
{
    if (beat_times_ms && beat_count > 0) {
        int closest_idx = 0;
        uint32_t min_diff = UINT32_MAX;
        for (int i = 0; i < beat_count; i++) {
            uint32_t beat_ms = beat_times_ms[i];
            uint32_t diff = position_ms > beat_ms ? position_ms - beat_ms : beat_ms - position_ms;
            if (diff < min_diff) {
                min_diff = diff;
                closest_idx = i;
            }
        }

        int target_idx = closest_idx + beat_shift;
        if (target_idx < 0) {
            target_idx = 0;
        }
        if (target_idx >= beat_count) {
            target_idx = beat_count - 1;
        }
        return beat_times_ms[target_idx];
    }

    uint16_t safe_bpm = bpm > 0 ? bpm : 120;
    int64_t beat_len_ms = 60000 / safe_bpm;
    int64_t target_ms = (int64_t)position_ms + (beat_len_ms * (int64_t)beat_shift);
    return target_ms > 0 ? (uint32_t)target_ms : 0u;
}

#ifndef UI_PERFORMANCE_TABS_HOST_TEST

#include "esp_log.h"
#include "ui_theme.h"

#define UI_PERFORMANCE_TAB_COUNT_HOT_CUES 8
#define UI_PERFORMANCE_TAB_COUNT_LOOPS 6

static const char *TAG = "ui_performance_tabs";
static ui_performance_tabs_config_t s_config;
static lv_obj_t *s_hot_cue_buttons[UI_PERFORMANCE_TAB_COUNT_HOT_CUES];
static lv_obj_t *s_loop_buttons[UI_PERFORMANCE_TAB_COUNT_LOOPS];
static lv_obj_t *s_label_loop_status = NULL;

static ui_controls_state_t *ui_performance_tabs_controls(void)
{
    return s_config.controls;
}

static uint8_t ui_performance_tabs_active_deck(void)
{
    return ui_controls_active_deck(ui_performance_tabs_controls());
}

static uint16_t ui_performance_tabs_active_bpm(void)
{
    return s_config.actions.active_bpm ? s_config.actions.active_bpm() : 120u;
}

static deck_state_t ui_performance_tabs_active_state(void)
{
    if (s_config.actions.active_state) {
        return s_config.actions.active_state();
    }
    return (deck_state_t){0};
}

static const anlz_metadata_t *ui_performance_tabs_active_anlz(void)
{
    return s_config.actions.active_anlz ? s_config.actions.active_anlz() : NULL;
}

static void ui_performance_tabs_format_time(char *out, size_t out_sz, uint32_t ms)
{
    uint32_t total_secs = ms / 1000u;
    uint32_t hrs = total_secs / 3600u;
    uint32_t mins = (total_secs % 3600u) / 60u;
    uint32_t secs = total_secs % 60u;
    snprintf(out, out_sz, "%02u:%02u:%02u",
             (unsigned)hrs,
             (unsigned)mins,
             (unsigned)secs);
}

static void ui_performance_tabs_label_small_caps(lv_obj_t *label,
                                                 const char *text,
                                                 lv_color_t color)
{
    if (!label) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

static lv_obj_t *ui_performance_tabs_value_label(lv_obj_t *parent,
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

static lv_obj_t *ui_performance_tabs_static_tile(lv_obj_t *parent,
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

static void ui_performance_tabs_style_hot_cue_pad(int index, bool is_loop, bool is_empty)
{
    (void)is_loop;
    if (index < 0 || index >= UI_PERFORMANCE_TAB_COUNT_HOT_CUES || !s_hot_cue_buttons[index]) {
        return;
    }

    static const uint32_t cue_hex_colors[UI_PERFORMANCE_TAB_COUNT_HOT_CUES] = {
        0x00E676, 0x00E5FF, 0xFFAB00, 0xE040FB,
        0xFFD600, 0xFF1744, 0x7C4DFF, 0x2979FF,
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

void ui_performance_tabs_init(const ui_performance_tabs_config_t *config)
{
    s_config = (ui_performance_tabs_config_t){0};
    if (config) {
        s_config = *config;
    }
    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_HOT_CUES; i++) {
        s_hot_cue_buttons[i] = NULL;
    }
    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_LOOPS; i++) {
        s_loop_buttons[i] = NULL;
    }
    s_label_loop_status = NULL;
}

void ui_performance_tabs_update_loop_screen_state(void)
{
    ui_controls_loop_state_t loop = ui_controls_active_loop(ui_performance_tabs_controls());

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

    static const int loop_beats[UI_PERFORMANCE_TAB_COUNT_LOOPS] = {1, 2, 4, 8, 16, 32};
    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_LOOPS; i++) {
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

void ui_performance_tabs_set_loop_shadow(uint8_t deck,
                                         bool active,
                                         uint32_t start_ms,
                                         uint32_t end_ms,
                                         int beats)
{
    uint8_t idx = deck < UI_PERFORMANCE_TARGET_DECK_COUNT ? deck : 0;
    ui_controls_set_loop_shadow(ui_performance_tabs_controls(), idx, active, start_ms, end_ms, beats);

    if (ui_controls_is_active_deck(ui_performance_tabs_controls(), idx)) {
        ui_performance_tabs_update_loop_screen_state();
    }
}

static void hot_cue_event_cb(lv_event_t *event)
{
    lv_obj_t *btn = lv_event_get_target(event);
    int cue_idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    ui_controls_hot_cue_t cue =
        ui_controls_hot_cue(ui_performance_tabs_controls(), (uint8_t)cue_idx);
    uint32_t pos = cue.position_ms;
    uint8_t deck = ui_performance_tabs_active_deck();

    if (cue.empty || pos == UI_CONTROLS_EMPTY_HOT_CUE_MS) {
        ESP_LOGI(TAG, "D%u Hot Cue %c is empty, ignoring click",
                 (unsigned)deck + 1u, 'A' + cue_idx);
        return;
    }

    uint32_t end_pos = cue.end_ms;
    if (cue.type == UI_CONTROLS_HOT_CUE_LOOP && end_pos > pos) {
        ui_performance_tabs_set_loop_shadow(deck, true, pos, end_pos, 0);
        if (s_config.actions.seek) {
            s_config.actions.seek(deck, pos);
        }
        if (s_config.actions.set_loop) {
            s_config.actions.set_loop(deck, pos, end_pos);
        }
        if (s_config.actions.play) {
            s_config.actions.play(deck);
        }
        ESP_LOGI(TAG, "D%u Hot Loop %c active: %lu - %lu ms",
                 (unsigned)deck + 1u, 'A' + cue_idx,
                 (unsigned long)pos, (unsigned long)end_pos);
    } else {
        ui_performance_tabs_set_loop_shadow(deck, false, 0, 0, 0);
        if (s_config.actions.clear_loop) {
            s_config.actions.clear_loop(deck);
        }
        if (s_config.actions.seek) {
            s_config.actions.seek(deck, pos);
        }
        if (s_config.actions.play) {
            s_config.actions.play(deck);
        }
        ESP_LOGI(TAG, "D%u Hot Cue %c triggered at %lu ms",
                 (unsigned)deck + 1u, 'A' + cue_idx, (unsigned long)pos);
    }
}

static void loop_btn_event_cb(lv_event_t *event)
{
    lv_obj_t *btn = lv_event_get_target(event);
    int beats = (int)(intptr_t)lv_obj_get_user_data(btn);
    uint8_t deck = ui_performance_tabs_active_deck();

    float bpm = (float)ui_performance_tabs_active_bpm();
    if (bpm <= 0.0f) {
        bpm = 120.0f;
    }
    uint32_t beat_len_ms = (uint32_t)(60000.0f / bpm);
    uint32_t start_ms = s_config.actions.deck_position_ms
                            ? s_config.actions.deck_position_ms(deck)
                            : ui_performance_tabs_active_state().position_ms;
    uint32_t end_ms = start_ms + (beat_len_ms * (uint32_t)beats);

    ui_performance_tabs_set_loop_shadow(deck, true, start_ms, end_ms, beats);
    if (s_config.actions.set_loop) {
        s_config.actions.set_loop(deck, start_ms, end_ms);
    }
    ESP_LOGI(TAG, "D%u Loop of %d beats active: %lu to %lu ms",
             (unsigned)deck + 1u, beats, (unsigned long)start_ms, (unsigned long)end_ms);
}

static void exit_loop_event_cb(lv_event_t *event)
{
    (void)event;
    uint8_t deck = ui_performance_tabs_active_deck();
    ui_performance_tabs_set_loop_shadow(deck, false, 0, 0, 0);
    if (s_config.actions.clear_loop) {
        s_config.actions.clear_loop(deck);
    }
    ESP_LOGI(TAG, "D%u Loop exited", (unsigned)deck + 1u);
}

static void jump_btn_event_cb(lv_event_t *event)
{
    lv_obj_t *btn = lv_event_get_target(event);
    int val = (int)(intptr_t)lv_obj_get_user_data(btn);
    uint8_t deck = ui_performance_tabs_active_deck();
    deck_state_t state = ui_performance_tabs_active_state();
    const anlz_metadata_t *meta = ui_performance_tabs_active_anlz();
    uint32_t new_pos = 0;

    if (meta && meta->beat_count > 0) {
        int closest_idx = 0;
        uint32_t min_diff = UINT32_MAX;
        for (int i = 0; i < meta->beat_count; i++) {
            uint32_t beat_ms = meta->beats[i].time_ms;
            uint32_t diff = state.position_ms > beat_ms
                                ? state.position_ms - beat_ms
                                : beat_ms - state.position_ms;
            if (diff < min_diff) {
                min_diff = diff;
                closest_idx = i;
            }
        }

        int target_idx = closest_idx + val;
        if (target_idx < 0) {
            target_idx = 0;
        }
        if (target_idx >= meta->beat_count) {
            target_idx = meta->beat_count - 1;
        }
        new_pos = meta->beats[target_idx].time_ms;
    } else {
        new_pos = ui_performance_tabs_calculate_jump_target(state.position_ms,
                                                            ui_performance_tabs_active_bpm(),
                                                            val,
                                                            NULL,
                                                            0);
    }

    if (s_config.actions.seek) {
        s_config.actions.seek(deck, new_pos);
    }
    ui_performance_tabs_set_loop_shadow(deck, false, 0, 0, 0);
    ESP_LOGI(TAG, "D%u Beat Jump from %lu ms to %lu ms, shift=%d beats",
             (unsigned)deck + 1u,
             (unsigned long)state.position_ms,
             (unsigned long)new_pos,
             val);
}

static lv_obj_t *ui_performance_tabs_create_screen(lv_obj_t *parent)
{
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    if (s_config.styles.screen_bg) {
        lv_obj_add_style(screen, s_config.styles.screen_bg, LV_PART_MAIN);
    }
    lv_obj_set_size(screen, s_config.hor_res, s_config.content_h);
    lv_obj_set_pos(screen, 0, s_config.content_y);
    return screen;
}

lv_obj_t *ui_performance_tabs_create_hot_cues(lv_obj_t *parent)
{
    lv_obj_t *screen = ui_performance_tabs_create_screen(parent);
    ui_controls_create_performance_target_selector(screen, 298, 4);

    int pad_w = 170;
    int pad_h = 130;
    int spacing_x = 20;
    int spacing_y = 20;
    int offset_x = 30;
    int offset_y = 48;

    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_HOT_CUES; i++) {
        int row = i / 4;
        int col = i % 4;

        s_hot_cue_buttons[i] = lv_button_create(screen);
        lv_obj_remove_style_all(s_hot_cue_buttons[i]);
        if (s_config.styles.pressed) {
            lv_obj_add_style(s_hot_cue_buttons[i], s_config.styles.pressed, LV_STATE_PRESSED);
        }
        ui_performance_tabs_style_hot_cue_pad(i, false, false);
        lv_obj_set_size(s_hot_cue_buttons[i], pad_w, pad_h);
        lv_obj_set_pos(s_hot_cue_buttons[i],
                       offset_x + col * (pad_w + spacing_x),
                       offset_y + row * (pad_h + spacing_y));
        lv_obj_set_user_data(s_hot_cue_buttons[i], (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_hot_cue_buttons[i], hot_cue_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_pad = lv_label_create(s_hot_cue_buttons[i]);
        lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
        lv_obj_set_style_text_font(lbl_pad, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_pad, COL_GREEN, LV_PART_MAIN);
        lv_obj_align(lbl_pad, LV_ALIGN_TOP_LEFT, 10, 10);

        lv_obj_t *lbl_time = lv_label_create(s_hot_cue_buttons[i]);
        char time_buf[16];
        ui_controls_hot_cue_t cue =
            ui_controls_hot_cue(ui_performance_tabs_controls(), (uint8_t)i);
        ui_performance_tabs_format_time(time_buf, sizeof(time_buf), cue.position_ms);
        lv_label_set_text(lbl_time, time_buf);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_time, COL_TEXT, LV_PART_MAIN);
        lv_obj_align(lbl_time, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }

    lv_obj_t *status_strip = lv_obj_create(screen);
    lv_obj_remove_style_all(status_strip);
    if (s_config.styles.panel_frame) {
        lv_obj_add_style(status_strip, s_config.styles.panel_frame, LV_PART_MAIN);
    }
    lv_obj_set_size(status_strip, 740, 62);
    lv_obj_set_pos(status_strip, 30, 360);
    lv_obj_clear_flag(status_strip, LV_OBJ_FLAG_SCROLLABLE);
    ui_performance_tabs_value_label(status_strip, "HOT CUE STATUS", COL_TEXT_MUTED,
                                    &lv_font_montserrat_12, 16, 12);
    ui_performance_tabs_static_tile(status_strip, 176, 12, 90, 36, "CUE A-H",
                                    COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_performance_tabs_static_tile(status_strip, 278, 12, 104, 36, "LOOP CUES",
                                    COL_AMBER, COL_PANEL_DK, COL_AMBER);
    ui_performance_tabs_static_tile(status_strip, 394, 12, 112, 36, "ANLZ DATA",
                                    COL_ACCENT, COL_PANEL_DK, COL_ACCENT);
    ui_performance_tabs_static_tile(status_strip, 518, 12, 142, 36, "D1/D2 TARGET",
                                    COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
    return screen;
}

lv_obj_t *ui_performance_tabs_create_beat_loop(lv_obj_t *parent)
{
    lv_obj_t *screen = ui_performance_tabs_create_screen(parent);
    ui_controls_create_performance_target_selector(screen, 20, 8);

    s_label_loop_status = lv_label_create(screen);
    ui_performance_tabs_label_small_caps(s_label_loop_status, "NO ACTIVE LOOP", COL_TEXT_DIM);
    lv_obj_align(s_label_loop_status, LV_ALIGN_TOP_MID, 0, 12);

    int loop_beats[UI_PERFORMANCE_TAB_COUNT_LOOPS] = {1, 2, 4, 8, 16, 32};
    const char *loop_labels[UI_PERFORMANCE_TAB_COUNT_LOOPS] = {
        "1 BEAT", "2 BEATS", "4 BEATS", "8 BEATS", "16 BEATS", "32 BEATS",
    };

    int pad_w = 210;
    int pad_h = 100;
    int spacing_x = 30;
    int spacing_y = 20;
    int offset_x = 60;
    int offset_y = 54;

    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_LOOPS; i++) {
        int row = i / 3;
        int col = i % 3;

        s_loop_buttons[i] = lv_button_create(screen);
        lv_obj_remove_style_all(s_loop_buttons[i]);
        if (s_config.styles.btn_secondary) {
            lv_obj_add_style(s_loop_buttons[i], s_config.styles.btn_secondary, LV_PART_MAIN);
        }
        if (s_config.styles.pressed) {
            lv_obj_add_style(s_loop_buttons[i], s_config.styles.pressed, LV_STATE_PRESSED);
        }
        lv_obj_set_size(s_loop_buttons[i], pad_w, pad_h);
        lv_obj_set_pos(s_loop_buttons[i],
                       offset_x + col * (pad_w + spacing_x),
                       offset_y + row * (pad_h + spacing_y));
        lv_obj_set_user_data(s_loop_buttons[i], (void *)(intptr_t)loop_beats[i]);
        lv_obj_add_event_cb(s_loop_buttons[i], loop_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_loop = lv_label_create(s_loop_buttons[i]);
        lv_label_set_text(lbl_loop, loop_labels[i]);
        lv_obj_set_style_text_font(lbl_loop, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_loop, COL_TEXT, LV_PART_MAIN);
        lv_obj_align(lbl_loop, LV_ALIGN_CENTER, 0, 0);
    }

    lv_obj_t *btn_exit = lv_button_create(screen);
    lv_obj_remove_style_all(btn_exit);
    lv_obj_set_style_bg_color(btn_exit, COL_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_exit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_exit, lv_color_hex(0xFF6B85), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_exit, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_exit, 6, LV_PART_MAIN);
    if (s_config.styles.pressed) {
        lv_obj_add_style(btn_exit, s_config.styles.pressed, LV_STATE_PRESSED);
    }
    lv_obj_set_size(btn_exit, 180, 50);
    lv_obj_set_pos(btn_exit, 310, 290);
    lv_obj_add_event_cb(btn_exit, exit_loop_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "EXIT LOOP");
    lv_obj_set_style_text_font(lbl_exit, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_exit, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_exit, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *loop_strip = lv_obj_create(screen);
    lv_obj_remove_style_all(loop_strip);
    if (s_config.styles.panel_frame) {
        lv_obj_add_style(loop_strip, s_config.styles.panel_frame, LV_PART_MAIN);
    }
    lv_obj_set_size(loop_strip, 740, 62);
    lv_obj_set_pos(loop_strip, 30, 360);
    lv_obj_clear_flag(loop_strip, LV_OBJ_FLAG_SCROLLABLE);
    ui_performance_tabs_value_label(loop_strip, "LOOP TOOLS", COL_TEXT_MUTED,
                                    &lv_font_montserrat_12, 16, 12);
    ui_performance_tabs_static_tile(loop_strip, 176, 12, 96, 36, "IN",
                                    COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_performance_tabs_static_tile(loop_strip, 284, 12, 96, 36, "OUT",
                                    COL_TEXT, COL_PANEL_DK, COL_BORDER);
    ui_performance_tabs_static_tile(loop_strip, 392, 12, 118, 36, "RELOOP",
                                    COL_DISABLED, COL_PANEL_DK, COL_BORDER);
    ui_performance_tabs_static_tile(loop_strip, 522, 12, 124, 36, "ACTIVE SIZE",
                                    COL_GREEN, COL_PANEL_DK, COL_GREEN);

    ui_performance_tabs_update_loop_screen_state();
    return screen;
}

static lv_obj_t *ui_performance_tabs_create_beat_jump_button(lv_obj_t *parent,
                                                             int x,
                                                             int y,
                                                             int value,
                                                             bool forward)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    if (s_config.styles.pressed) {
        lv_obj_add_style(btn, s_config.styles.pressed, LV_STATE_PRESSED);
    }
    lv_obj_set_size(btn, 150, 82);
    lv_obj_set_pos(btn, x, y);

    lv_color_t accent = forward ? COL_GREEN : COL_RED;
    lv_color_t fill = forward ? lv_color_hex(0x10251B) : lv_color_hex(0x2A1016);
    lv_obj_set_style_bg_color(btn, fill, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_set_user_data(btn, (void *)(intptr_t)value);
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

lv_obj_t *ui_performance_tabs_create_beat_jump(lv_obj_t *parent)
{
    lv_obj_t *screen = ui_performance_tabs_create_screen(parent);
    ui_controls_create_performance_target_selector(screen, 298, 0);

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

    lv_obj_t *lane_back = lv_obj_create(screen);
    lv_obj_remove_style_all(lane_back);
    if (s_config.styles.panel_frame) {
        lv_obj_add_style(lane_back, s_config.styles.panel_frame, LV_PART_MAIN);
    }
    lv_obj_set_size(lane_back, lane_w, lane_h);
    lv_obj_set_pos(lane_back, lane_x, lane_top_y);
    lv_obj_clear_flag(lane_back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_back = lv_label_create(lane_back);
    ui_performance_tabs_label_small_caps(lbl_back, "BACKWARD", COL_AMBER);
    lv_obj_set_pos(lbl_back, 16, 12);

    for (int i = 0; i < 4; i++) {
        ui_performance_tabs_create_beat_jump_button(lane_back,
                                                    btn_x0 + i * (btn_w + spacing_x),
                                                    btn_y,
                                                    -jump_vals[i],
                                                    false);
    }

    lv_obj_t *lane_forward = lv_obj_create(screen);
    lv_obj_remove_style_all(lane_forward);
    if (s_config.styles.panel_frame) {
        lv_obj_add_style(lane_forward, s_config.styles.panel_frame, LV_PART_MAIN);
    }
    lv_obj_set_size(lane_forward, lane_w, lane_h);
    lv_obj_set_pos(lane_forward, lane_x, lane_top_y + lane_h + lane_gap);
    lv_obj_clear_flag(lane_forward, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_forward = lv_label_create(lane_forward);
    ui_performance_tabs_label_small_caps(lbl_forward, "FORWARD", COL_GREEN);
    lv_obj_set_pos(lbl_forward, 16, 12);

    for (int i = 0; i < 4; i++) {
        ui_performance_tabs_create_beat_jump_button(lane_forward,
                                                    btn_x0 + i * (btn_w + spacing_x),
                                                    btn_y,
                                                    jump_vals[i],
                                                    true);
    }

    lv_obj_t *jump_strip = lv_obj_create(screen);
    lv_obj_remove_style_all(jump_strip);
    if (s_config.styles.panel_frame) {
        lv_obj_add_style(jump_strip, s_config.styles.panel_frame, LV_PART_MAIN);
    }
    lv_obj_set_size(jump_strip, 720, 62);
    lv_obj_set_pos(jump_strip, 40, 346);
    lv_obj_clear_flag(jump_strip, LV_OBJ_FLAG_SCROLLABLE);
    ui_performance_tabs_value_label(jump_strip, "GRID / QUANTIZE", COL_TEXT_MUTED,
                                    &lv_font_montserrat_12, 16, 12);
    ui_performance_tabs_static_tile(jump_strip, 184, 12, 106, 36, "BEAT GRID",
                                    COL_ACCENT, COL_PANEL_DK, COL_ACCENT);
    ui_performance_tabs_static_tile(jump_strip, 302, 12, 110, 36, "QUANTIZE",
                                    COL_DISABLED, COL_PANEL_DK, COL_BORDER);
    ui_performance_tabs_static_tile(jump_strip, 424, 12, 116, 36, "SNAP: ON",
                                    COL_GREEN, COL_PANEL_DK, COL_GREEN);
    ui_performance_tabs_static_tile(jump_strip, 552, 12, 112, 36, "D1/D2",
                                    COL_TEXT, COL_PANEL_DK, COL_BORDER_LT);
    return screen;
}

void ui_performance_tabs_update_hot_cues(void)
{
    uint8_t deck = ui_performance_tabs_active_deck();
    const anlz_metadata_t *meta = ui_performance_tabs_active_anlz();
    bool has_anlz = meta != NULL;

    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_HOT_CUES; i++) {
        bool found = false;
        uint32_t pos = 0;
        uint32_t end_pos = 0;
        uint8_t type = UI_CONTROLS_HOT_CUE_SINGLE;

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
            ui_controls_set_hot_cue(ui_performance_tabs_controls(),
                                    (uint8_t)i,
                                    pos,
                                    end_pos,
                                    type,
                                    false);

            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                char time_buf[16];
                ui_performance_tabs_format_time(time_buf, sizeof(time_buf), pos);
                lv_label_set_text(lbl_time, time_buf);
            }

            lv_obj_t *lbl_pad = lv_obj_get_child(s_hot_cue_buttons[i], 0);
            bool is_loop = type == UI_CONTROLS_HOT_CUE_LOOP;
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "%s %c", is_loop ? "LOOP" : "CUE", 'A' + i);
            }
            ui_performance_tabs_style_hot_cue_pad(i, is_loop, false);
        } else if (has_anlz) {
            ui_controls_set_hot_cue(ui_performance_tabs_controls(),
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
            ui_performance_tabs_style_hot_cue_pad(i, false, true);
        } else {
            uint32_t default_pos = (uint32_t)i * 15000u;
            if (i >= 5) {
                default_pos = (uint32_t)(i - 1) * 30000u;
            }
            ui_controls_set_hot_cue(ui_performance_tabs_controls(),
                                    (uint8_t)i,
                                    default_pos,
                                    0,
                                    UI_CONTROLS_HOT_CUE_SINGLE,
                                    false);
            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                char time_buf[16];
                ui_performance_tabs_format_time(time_buf, sizeof(time_buf), default_pos);
                lv_label_set_text(lbl_time, time_buf);
            }
            lv_obj_t *lbl_pad = lv_obj_get_child(s_hot_cue_buttons[i], 0);
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
            }
            ui_performance_tabs_style_hot_cue_pad(i, false, false);
        }
    }

    if (s_config.actions.update_overview_cue_markers) {
        s_config.actions.update_overview_cue_markers(deck);
    }
}

#endif
