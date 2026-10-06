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
static lv_obj_t *s_jog_mode_label;
static uint8_t s_jog_mode_deck = CTRL_DECK_NONE;
static bool s_jog_mode_cdj;
static bool s_pad_held[UI_PERFORMANCE_TAB_COUNT_HOT_CUES];
static uint8_t s_pad_deck[UI_PERFORMANCE_TAB_COUNT_HOT_CUES];
static bool s_delete_mode;
static lv_obj_t *s_delete_label;
static lv_obj_t *s_memory_panel;
static lv_obj_t *s_memory_text;
static lv_obj_t *s_memory_title;
static bool s_restore_held;
static uint8_t s_restore_deck;
static bool s_display_valid;
static uint8_t s_display_deck;
static uint32_t s_display_version, s_display_revision;
static media_persistent_id_t s_display_id;

void ui_performance_tabs_cancel_holds(void)
{
    for (uint8_t i = 0; i < UI_PERFORMANCE_TAB_COUNT_HOT_CUES; ++i) {
        if (s_pad_held[i] && s_config.actions.hot_cue_pad)
            s_config.actions.hot_cue_pad(s_pad_deck[i], i, false, false);
        s_pad_held[i] = false;
    }
    s_restore_held = false;
    s_delete_mode = false;
    if (s_delete_label) lv_label_set_text(s_delete_label, "DELETE: OFF");
    if (s_memory_panel) lv_obj_add_flag(s_memory_panel, LV_OBJ_FLAG_HIDDEN);
}

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
    s_jog_mode_label = NULL;
    s_delete_label = NULL;
    s_memory_panel = NULL;
    s_memory_title = NULL;
    s_memory_text = NULL;
    s_display_valid = false;
    s_restore_held = false;
    s_delete_mode = false;
    for (unsigned i = 0; i < UI_PERFORMANCE_TAB_COUNT_HOT_CUES; ++i) s_pad_held[i] = false;
    s_jog_mode_deck = CTRL_DECK_NONE;
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
    uint8_t deck = ui_performance_tabs_active_deck();
    if (s_config.actions.hot_cue_pad) {
        lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_PRESSED) {
            if (s_delete_mode) {
                s_config.actions.hot_cue_pad(deck, (uint8_t)cue_idx, true, true);
                s_config.actions.hot_cue_pad(deck, (uint8_t)cue_idx, false, true);
            } else {
                s_pad_held[cue_idx] = true;
                s_pad_deck[cue_idx] = deck;
                s_config.actions.hot_cue_pad(deck, (uint8_t)cue_idx, true, false);
            }
        } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
                   s_pad_held[cue_idx]) {
            s_pad_held[cue_idx] = false;
            s_config.actions.hot_cue_pad(s_pad_deck[cue_idx], (uint8_t)cue_idx, false, false);
        }
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ui_controls_hot_cue_t cue =
        ui_controls_hot_cue(ui_performance_tabs_controls(), (uint8_t)cue_idx);
    uint32_t pos = cue.position_ms;

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

static void jog_mode_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_config.actions.set_jog_mode && s_config.actions.active_state) {
        deck_state_t state = s_config.actions.active_state();
        s_config.actions.set_jog_mode(ui_performance_tabs_active_deck(),
                                     !state.jog_cdj_mode);
    }
}

void ui_performance_tabs_update_jog_mode(void)
{
    if (!s_jog_mode_label || !s_config.actions.active_state) return;
    uint8_t deck = ui_performance_tabs_active_deck();
    bool cdj = s_config.actions.active_state().jog_cdj_mode;
    if (deck == s_jog_mode_deck && cdj == s_jog_mode_cdj) return;
    s_jog_mode_deck = deck;
    s_jog_mode_cdj = cdj;
    lv_label_set_text_fmt(s_jog_mode_label, "D%u JOG: %s",
                         (unsigned)deck + 1u, cdj ? "CDJ" : "VINYL");
    lv_obj_center(s_jog_mode_label);
}

static void restore_source_cues_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_restore_held = true;
        s_restore_deck = ui_performance_tabs_active_deck();
    } else if (code == LV_EVENT_LONG_PRESSED && s_restore_held &&
               s_restore_deck == ui_performance_tabs_active_deck() &&
               s_config.actions.restore_source_cues) {
        s_restore_held = false;
        s_config.actions.restore_source_cues(s_restore_deck);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_restore_held = false;
    }
}

static void delete_mode_event_cb(lv_event_t *event)
{
    (void)event;
    s_delete_mode = !s_delete_mode;
    lv_label_set_text(s_delete_label, s_delete_mode ? "DELETE: ON" : "DELETE: OFF");
}

static void memory_event_cb(lv_event_t *event)
{
    (void)event;
    if (lv_obj_has_flag(s_memory_panel, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(s_memory_panel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_memory_panel, LV_OBJ_FLAG_HIDDEN);
    ui_performance_tabs_update_hot_cues();
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
    ui_controls_create_performance_target_selector(screen, (s_config.hor_res - 204) / 2, 4);

    int pad_w = (s_config.hor_res - 120) / 4;
    int pad_h = 130 + (s_config.content_h - 434) / 2;
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
        lv_obj_add_event_cb(s_hot_cue_buttons[i], hot_cue_event_cb, LV_EVENT_ALL, NULL);

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
    lv_obj_set_size(status_strip, s_config.hor_res - 60, 62);
    lv_obj_set_pos(status_strip, 30, s_config.content_h - 74);
    lv_obj_clear_flag(status_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *del = lv_button_create(status_strip);
    lv_obj_set_size(del, 144, 36);
    lv_obj_set_pos(del, 12, 12);
    lv_obj_set_style_bg_color(del, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_add_event_cb(del, delete_mode_event_cb, LV_EVENT_CLICKED, NULL);
    s_delete_label = ui_performance_tabs_value_label(del, "DELETE: OFF", COL_TEXT,
                                                    &lv_font_montserrat_12, 0, 0);
    lv_obj_center(s_delete_label);
    ui_performance_tabs_static_tile(status_strip, 176, 12, 90, 36, "CUE A-H",
                                    COL_GREEN, COL_PANEL_DK, COL_GREEN);
    lv_obj_t *memory = lv_button_create(status_strip);
    lv_obj_set_size(memory, 104, 36);
    lv_obj_set_pos(memory, 278, 12);
    lv_obj_set_style_bg_color(memory, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_add_event_cb(memory, memory_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *memory_label = ui_performance_tabs_value_label(memory, "MEMORY", COL_AMBER,
                                                           &lv_font_montserrat_12, 0, 0);
    lv_obj_center(memory_label);
    lv_obj_t *restore = lv_button_create(status_strip);
    lv_obj_remove_style_all(restore);
    lv_obj_set_style_bg_color(restore, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(restore, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(restore, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(restore, 1, LV_PART_MAIN);
    lv_obj_set_size(restore, 112, 36);
    lv_obj_set_pos(restore, 394, 12);
    lv_obj_add_event_cb(restore, restore_source_cues_event_cb,
                        LV_EVENT_ALL, NULL);
    lv_obj_t *restore_label = lv_label_create(restore);
    lv_label_set_text(restore_label, "HOLD RESTORE");
    lv_obj_set_style_text_font(restore_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(restore_label, COL_ACCENT, LV_PART_MAIN);
    lv_obj_center(restore_label);
    lv_obj_t *jog_mode = lv_button_create(status_strip);
    lv_obj_remove_style_all(jog_mode);
    lv_obj_set_style_bg_color(jog_mode, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(jog_mode, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(jog_mode, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_set_style_border_width(jog_mode, 1, LV_PART_MAIN);
    lv_obj_set_size(jog_mode, 142, 36);
    lv_obj_set_pos(jog_mode, 518, 12);
    lv_obj_add_event_cb(jog_mode, jog_mode_event_cb, LV_EVENT_CLICKED, NULL);
    s_jog_mode_label = lv_label_create(jog_mode);
    lv_obj_set_style_text_font(s_jog_mode_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_jog_mode_label, COL_TEXT, LV_PART_MAIN);
    s_jog_mode_deck = CTRL_DECK_NONE;
    ui_performance_tabs_update_jog_mode();
    s_memory_panel = lv_obj_create(screen);
    lv_obj_set_size(s_memory_panel, s_config.hor_res - 60, s_config.content_h - 100);
    lv_obj_set_pos(s_memory_panel, 30, 48);
    lv_obj_set_style_bg_color(s_memory_panel, COL_PANEL_DK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_memory_panel, LV_OPA_COVER, LV_PART_MAIN);
    s_memory_title = ui_performance_tabs_value_label(s_memory_panel, "MEMORY CUES", COL_AMBER,
                                                     &lv_font_montserrat_16, 0, 0);
    s_memory_text = ui_performance_tabs_value_label(s_memory_panel, "", COL_TEXT,
                                                   &lv_font_montserrat_14, 0, 30);
    lv_obj_set_style_text_line_space(s_memory_text, 6, LV_PART_MAIN);
    lv_obj_add_flag(s_memory_panel, LV_OBJ_FLAG_HIDDEN);
    return screen;
}

void ui_performance_tabs_update_hot_cues(void)
{
    uint8_t deck = ui_performance_tabs_active_deck();
    anlz_snapshot_t *snapshot =
        ui_performance_tabs_acquire_active_anlz();
    const anlz_metadata_t *meta = anlz_snapshot_metadata(snapshot);
    media_persistent_id_t id = {0};
    bool has_identity = s_config.actions.active_persistent_id &&
        s_config.actions.active_persistent_id(&id);
    uint32_t version = anlz_snapshot_version(snapshot), revision = deck_core_hot_cue_revision();
    if (s_display_valid && s_display_deck == deck && s_display_version == version &&
        s_display_revision == revision &&
        ((!id.valid && !s_display_id.valid) || media_persistent_id_equal(&id, &s_display_id))) {
        anlz_snapshot_release(snapshot);
        return;
    }
    if (s_memory_title) {
        lv_label_set_text_fmt(s_memory_title, "D%u MEMORY CUES: %u%s",
            (unsigned)deck + 1u, meta ? meta->memory_cue_count : 0u,
            meta && meta->memory_cues_truncated ? " (TRUNCATED)" : "");
        char text[1024] = {0};
        size_t used = 0;
        for (uint8_t i = 0; i < ANLZ_MAX_MEMORY_CUES; ++i) {
            if (meta && i < meta->memory_cue_count) {
                const anlz_cue_t *cue = &meta->memory_cues[i];
                char start[16], end[16];
                ui_performance_tabs_format_time(start, sizeof start, cue->start_ms);
                ui_performance_tabs_format_time(end, sizeof end, cue->end_ms);
                int written = snprintf(text + used, sizeof text - used, "%02u  %s  %s%s%s\n",
                    (unsigned)i + 1u, start, cue->type == ANLZ_CUE_LOOP ? "LOOP " : "CUE",
                    cue->type == ANLZ_CUE_LOOP ? "- " : "", cue->type == ANLZ_CUE_LOOP ? end : "");
                if (written < 0 || (size_t)written >= sizeof text - used) break;
                used += (size_t)written;
            }
        }
        lv_label_set_text(s_memory_text, text[0] ? text : "No memory cues");
    }
    hot_cue_store_blob_t source = {0};
    hot_cue_store_blob_t local = {0};
    hot_cue_store_blob_t effective = {0};
    if (meta) {
        for (uint8_t j = 0; j < meta->cue_count; ++j) {
            const anlz_cue_t *cue = &meta->cues[j];
            if (cue->index >= UI_PERFORMANCE_TAB_COUNT_HOT_CUES ||
                (cue->type != ANLZ_CUE_SINGLE && cue->type != ANLZ_CUE_LOOP)) continue;
            const uint32_t bit = 1u << cue->index;
            source.valid_mask |= bit;
            source.slots[cue->index] = (hot_cue_store_slot_t) {
                .pos_ms = cue->start_ms,
                .end_ms = cue->type == ANLZ_CUE_LOOP ? cue->end_ms : 0u,
                .type = cue->type == ANLZ_CUE_LOOP ?
                    HOT_CUE_STORE_TYPE_LOOP : HOT_CUE_STORE_TYPE_SINGLE,
            };
        }
    }
    esp_err_t local_rc = has_identity ? hot_cue_store_load(&id, &local) : ESP_ERR_NOT_FOUND;
    bool has_local = local_rc == ESP_OK;
    if (has_identity && local_rc != ESP_OK && local_rc != ESP_ERR_NOT_FOUND) {
        /* Match deck_core's fail-closed bank on a corrupt/colliding edit record. */
        source.valid_mask = 0u;
        ESP_LOGW(TAG, "D%u hot cue refresh failed: %s",
                 (unsigned)deck + 1u, esp_err_to_name(local_rc));
    }
    hot_cue_store_merge(&source, has_local ? &local : NULL, &effective);

    for (int i = 0; i < UI_PERFORMANCE_TAB_COUNT_HOT_CUES; i++) {
        bool found = false;
        uint32_t pos = 0;
        uint32_t end_pos = 0;
        uint8_t type = UI_CONTROLS_HOT_CUE_SINGLE;

        if ((effective.valid_mask & (1u << i)) != 0u) {
            pos = effective.slots[i].pos_ms;
            end_pos = effective.slots[i].end_ms;
            type = effective.slots[i].type;
            found = true;
        }

        if (found) {
            ui_controls_set_hot_cue(ui_performance_tabs_controls(),
                                    (uint8_t)i,
                                    pos,
                                    end_pos,
                                    type,
                                    false);

            lv_obj_t *lbl_time = s_hot_cue_buttons[i] ? lv_obj_get_child(s_hot_cue_buttons[i], 1) : NULL;
            if (lbl_time) {
                char time_buf[16];
                ui_performance_tabs_format_time(time_buf, sizeof(time_buf), pos);
                lv_label_set_text(lbl_time, time_buf);
            }

            lv_obj_t *lbl_pad = s_hot_cue_buttons[i] ? lv_obj_get_child(s_hot_cue_buttons[i], 0) : NULL;
            bool is_loop = type == UI_CONTROLS_HOT_CUE_LOOP;
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "%s %c", is_loop ? "LOOP" : "CUE", 'A' + i);
            }
            ui_performance_tabs_style_hot_cue_pad(i, is_loop, false);
        } else {
            ui_controls_set_hot_cue(ui_performance_tabs_controls(),
                                    (uint8_t)i,
                                    0,
                                    0,
                                    UI_CONTROLS_HOT_CUE_SINGLE,
                                    true);
            lv_obj_t *lbl_time = s_hot_cue_buttons[i] ? lv_obj_get_child(s_hot_cue_buttons[i], 1) : NULL;
            if (lbl_time) {
                lv_label_set_text(lbl_time, "EMPTY");
            }
            lv_obj_t *lbl_pad = s_hot_cue_buttons[i] ? lv_obj_get_child(s_hot_cue_buttons[i], 0) : NULL;
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
            }
            ui_performance_tabs_style_hot_cue_pad(i, false, true);
        }
    }

    if (s_config.actions.update_overview_cue_markers) {
        s_config.actions.update_overview_cue_markers(deck);
    }
    anlz_snapshot_release(snapshot);
    s_display_id = id;
    s_display_deck = deck;
    s_display_version = version;
    s_display_revision = revision;
    s_display_valid = true;
}

#endif
