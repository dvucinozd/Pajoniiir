#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "deck_core.h"
#include "esp_err.h"
#include "rekordbox_anlz.h"
#include "ui_frame_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_style_t *screen_bg;
    lv_style_t *panel_frame;
    lv_style_t *btn_primary;
    lv_style_t *btn_amber;
    lv_style_t *pressed;
} ui_overview_styles_t;

typedef struct {
    void (*select_deck)(uint8_t deck);
    void (*play_pause)(uint8_t deck);
    void (*cue)(uint8_t deck);
    void (*seek)(uint8_t deck, uint32_t target_ms);
} ui_overview_actions_t;

typedef struct {
    ui_overview_styles_t styles;
    ui_overview_actions_t actions;
} ui_overview_config_t;

void ui_overview_init(const ui_overview_config_t *config);
lv_obj_t *ui_overview_create(lv_obj_t *parent);
void ui_overview_set_performance_target(uint8_t active_deck);

void ui_overview_load_waveform_data(uint8_t deck,
                                    uint32_t duration_ms,
                                    const uint8_t waveform_low[400],
                                    bool has_waveform,
                                    const anlz_metadata_t *meta);

void ui_overview_update_cue_markers(uint8_t deck,
                                    const anlz_metadata_t *meta,
                                    uint32_t duration_ms);
void ui_overview_update(const ui_frame_context_t *ctx);

#ifdef __cplusplus
}
#endif
