#include "ui_controls.h"

#include <string.h>

static uint8_t ui_controls_valid_deck(uint8_t deck)
{
    return deck < UI_PERFORMANCE_TARGET_DECK_COUNT ? deck : UI_PERFORMANCE_TARGET_DEFAULT_DECK;
}

void ui_controls_state_init(ui_controls_state_t *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    ui_performance_target_init(&state->performance_target);

    static const uint32_t default_positions[UI_CONTROLS_HOT_CUE_COUNT] = {
        0,
        15000,
        30000,
        45000,
        60000,
        90000,
        120000,
        150000,
    };
    for (uint8_t i = 0; i < UI_CONTROLS_HOT_CUE_COUNT; i++) {
        state->hot_cue[i] = (ui_controls_hot_cue_t){
            .empty = false,
            .position_ms = default_positions[i],
            .end_ms = 0,
            .type = UI_CONTROLS_HOT_CUE_SINGLE,
        };
    }
}

uint8_t ui_controls_active_deck(const ui_controls_state_t *state)
{
    return state ? ui_performance_target_get(&state->performance_target)
                 : UI_PERFORMANCE_TARGET_DEFAULT_DECK;
}

bool ui_controls_set_active_deck(ui_controls_state_t *state, uint8_t deck)
{
    if (!state || deck >= UI_PERFORMANCE_TARGET_DECK_COUNT) {
        return false;
    }

    uint8_t before = ui_performance_target_get(&state->performance_target);
    ui_performance_target_set(&state->performance_target, deck);
    return ui_performance_target_get(&state->performance_target) != before;
}

bool ui_controls_is_active_deck(const ui_controls_state_t *state, uint8_t deck)
{
    return state && ui_performance_target_is_active(&state->performance_target, deck);
}

void ui_controls_set_loop_shadow(ui_controls_state_t *state,
                                 uint8_t deck,
                                 bool active,
                                 uint32_t start_ms,
                                 uint32_t end_ms,
                                 int beats)
{
    if (!state) {
        return;
    }

    uint8_t idx = ui_controls_valid_deck(deck);
    state->loop[idx] = (ui_controls_loop_state_t){
        .active = active,
        .start_ms = start_ms,
        .end_ms = end_ms,
        .beats = beats,
    };
}

ui_controls_loop_state_t ui_controls_loop_for_deck(const ui_controls_state_t *state,
                                                   uint8_t deck)
{
    if (!state) {
        return (ui_controls_loop_state_t){0};
    }
    return state->loop[ui_controls_valid_deck(deck)];
}

ui_controls_loop_state_t ui_controls_active_loop(const ui_controls_state_t *state)
{
    return ui_controls_loop_for_deck(state, ui_controls_active_deck(state));
}

void ui_controls_set_hot_cue(ui_controls_state_t *state,
                             uint8_t index,
                             uint32_t position_ms,
                             uint32_t end_ms,
                             uint8_t type,
                             bool empty)
{
    if (!state || index >= UI_CONTROLS_HOT_CUE_COUNT) {
        return;
    }

    state->hot_cue[index] = (ui_controls_hot_cue_t){
        .empty = empty,
        .position_ms = empty ? UI_CONTROLS_EMPTY_HOT_CUE_MS : position_ms,
        .end_ms = empty ? 0 : end_ms,
        .type = type,
    };
}

ui_controls_hot_cue_t ui_controls_hot_cue(const ui_controls_state_t *state,
                                          uint8_t index)
{
    if (!state || index >= UI_CONTROLS_HOT_CUE_COUNT) {
        return (ui_controls_hot_cue_t){
            .empty = true,
            .position_ms = UI_CONTROLS_EMPTY_HOT_CUE_MS,
            .end_ms = 0,
            .type = UI_CONTROLS_HOT_CUE_SINGLE,
        };
    }
    return state->hot_cue[index];
}

#ifndef UI_CONTROLS_HOST_TEST

#include <stdint.h>

#include "deck_core.h"
#include "lvgl.h"
#include "ui_theme.h"

static ui_controls_widget_config_t s_widget_config;
static lv_obj_t *s_perf_target_buttons[10];
static size_t s_perf_target_button_count = 0;

static void ui_controls_label_small_caps(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
}

void ui_controls_widgets_init(const ui_controls_widget_config_t *config)
{
    memset(&s_widget_config, 0, sizeof(s_widget_config));
    if (config) {
        s_widget_config = *config;
    }
    memset(s_perf_target_buttons, 0, sizeof(s_perf_target_buttons));
    s_perf_target_button_count = 0;
}

void ui_controls_update_performance_target_visuals(const ui_controls_state_t *state)
{
    uint8_t active_deck = ui_controls_active_deck(state);

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

    if (s_widget_config.set_overview_target) {
        s_widget_config.set_overview_target(active_deck);
    }
}

static void perf_target_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t deck = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    if (s_widget_config.select_deck) {
        s_widget_config.select_deck(deck);
    }
}

void ui_controls_create_performance_target_selector(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_controls_label_small_caps(label, "TARGET", COL_TEXT_MUTED);
    lv_obj_set_pos(label, x, y + 8);

    for (uint8_t deck = 0; deck < UI_PERFORMANCE_TARGET_DECK_COUNT; deck++) {
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_remove_style_all(btn);
        if (s_widget_config.pressed) {
            lv_obj_add_style(btn, s_widget_config.pressed, LV_STATE_PRESSED);
        }
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
}

#endif
