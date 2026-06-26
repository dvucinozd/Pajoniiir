#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ui_settings_should_poll(uint32_t now_ms,
                             uint32_t last_poll_ms,
                             bool force,
                             uint32_t interval_ms);
uint8_t ui_settings_master_trim_preset_count(void);
uint8_t ui_settings_master_trim_sanitize_preset(uint8_t preset);
uint8_t ui_settings_master_trim_next_preset(uint8_t current);
float ui_settings_master_trim_gain(uint8_t preset);
const char *ui_settings_master_trim_label(uint8_t preset);

#ifndef UI_SETTINGS_HOST_TEST

#include "lvgl.h"
#include "ui_frame_context.h"

typedef struct {
    lv_style_t *screen_bg;
    lv_style_t *panel_frame;
    lv_style_t *btn_secondary;
    lv_style_t *pressed;
    int hor_res;
    int content_y;
    int content_h;
} ui_settings_config_t;

typedef struct {
    lv_obj_t *uart_status;
    lv_obj_t *sd_status;
} ui_settings_widgets_t;

void ui_settings_configure(const ui_settings_config_t *config);
lv_obj_t *ui_settings_create(lv_obj_t *parent);
void ui_settings_init(const ui_settings_widgets_t *widgets);
void ui_settings_invalidate(void);
void ui_settings_update(const ui_frame_context_t *ctx);
void ui_settings_refresh_storage(void);

#endif
