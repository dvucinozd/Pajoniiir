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
const char *ui_settings_s3_debug_ap_status_label(uint8_t status);
bool ui_settings_is_active_tab(int active_tab, int settings_tab_index);

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
    int settings_tab_index;
} ui_settings_config_t;

typedef struct {
    lv_obj_t *uart_status;
    lv_obj_t *sd_status;
} ui_settings_widgets_t;

// Action invoked when the user flips the Wi-Fi remote switch. Registered by
// app_main so the UI stays decoupled from the wifi_link/web_server transport
// (avoids a ui -> wifi_link -> web_server -> ui component dependency cycle).
typedef void (*ui_settings_wifi_toggle_cb_t)(bool enable);
void ui_settings_set_wifi_toggle_cb(ui_settings_wifi_toggle_cb_t cb);

typedef void (*ui_settings_s3_debug_ap_toggle_cb_t)(bool enable);
void ui_settings_set_s3_debug_ap_toggle_cb(ui_settings_s3_debug_ap_toggle_cb_t cb);
void ui_settings_set_s3_debug_ap_status(uint8_t status);

void ui_settings_configure(const ui_settings_config_t *config);
lv_obj_t *ui_settings_create(lv_obj_t *parent);
void ui_settings_init(const ui_settings_widgets_t *widgets);
void ui_settings_invalidate(void);
void ui_settings_update(const ui_frame_context_t *ctx);
void ui_settings_refresh_storage(void);

#endif
