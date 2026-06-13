#pragma once

#include <stdbool.h>
#include <stdint.h>

uint32_t ui_performance_tabs_calculate_jump_target(uint32_t position_ms,
                                                   uint16_t bpm,
                                                   int beat_shift,
                                                   const uint32_t *beat_times_ms,
                                                   int beat_count);

#ifndef UI_PERFORMANCE_TABS_HOST_TEST

#include "deck_core.h"
#include "lvgl.h"
#include "rekordbox_anlz.h"
#include "ui_controls.h"

typedef struct {
    lv_style_t *screen_bg;
    lv_style_t *panel_frame;
    lv_style_t *btn_secondary;
    lv_style_t *pressed;
} ui_performance_tabs_styles_t;

typedef struct {
    uint16_t (*active_bpm)(void);
    const anlz_metadata_t *(*active_anlz)(void);
    deck_state_t (*active_state)(void);
    uint32_t (*deck_position_ms)(uint8_t deck);
    void (*seek)(uint8_t deck, uint32_t position_ms);
    void (*play)(uint8_t deck);
    void (*set_loop)(uint8_t deck, uint32_t start_ms, uint32_t end_ms);
    void (*clear_loop)(uint8_t deck);
    void (*toggle_master_tempo)(void);
    void (*update_overview_cue_markers)(uint8_t deck);
} ui_performance_tabs_actions_t;

typedef struct {
    ui_controls_state_t *controls;
    ui_performance_tabs_styles_t styles;
    ui_performance_tabs_actions_t actions;
    int hor_res;
    int content_y;
    int content_h;
} ui_performance_tabs_config_t;

void ui_performance_tabs_init(const ui_performance_tabs_config_t *config);
lv_obj_t *ui_performance_tabs_create_hot_cues(lv_obj_t *parent);
lv_obj_t *ui_performance_tabs_create_beat_loop(lv_obj_t *parent);
lv_obj_t *ui_performance_tabs_create_beat_jump(lv_obj_t *parent);
lv_obj_t *ui_performance_tabs_create_key_shift(lv_obj_t *parent);
void ui_performance_tabs_update_hot_cues(void);
void ui_performance_tabs_update_loop_screen_state(void);
void ui_performance_tabs_set_loop_shadow(uint8_t deck,
                                         bool active,
                                         uint32_t start_ms,
                                         uint32_t end_ms,
                                         int beats);

#endif
