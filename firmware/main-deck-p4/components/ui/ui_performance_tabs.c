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

static const char *TAG = "ui_performance_tabs";
static ui_performance_tabs_config_t s_config;
static lv_obj_t *s_hot_cue_buttons[UI_PERFORMANCE_TAB_COUNT_HOT_CUES];

static ui_controls_state_t *ui_performance_tabs_controls(void)
{
    return s_config.controls;
}

static uint8_t ui_performance_tabs_active_deck(void)
{
    return ui_controls_active_deck(ui_performance_tabs_controls());
}

static anlz_snapshot_t *ui_performance_tabs_acquire_active_anlz(void)
{
    return s_config.actions.acquire_active_anlz
               ? s_config.actions.acquire_active_anlz()
               : NULL;
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
}

void ui_performance_tabs_set_loop_shadow(uint8_t deck,
                                         bool active,
                                         uint32_t start_ms,
                                         uint32_t end_ms,
                                         int beats)
{
    uint8_t idx = deck < UI_PERFORMANCE_TARGET_DECK_COUNT ? deck : 0;
    ui_controls_set_loop_shadow(ui_performance_tabs_controls(), idx, active, start_ms, end_ms, beats);
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

void ui_performance_tabs_update_hot_cues(void)
{
    uint8_t deck = ui_performance_tabs_active_deck();
    anlz_snapshot_t *snapshot =
        ui_performance_tabs_acquire_active_anlz();
    const anlz_metadata_t *meta = anlz_snapshot_metadata(snapshot);
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
    anlz_snapshot_release(snapshot);
}

#endif
