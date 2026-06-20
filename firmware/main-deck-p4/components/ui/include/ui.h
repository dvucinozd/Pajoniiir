#pragma once

// LVGL DJ UI — landscape 800x480 after 90° rotation.
//
// Tabs (matching upstream XDJ100SX skin):
//   overview | library | hotcues | beatloop | beatjump | keyshift | settings

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t ui_init(void);
void      ui_update(void);   // call from LVGL timer task with current deck_state
void      ui_refresh_library(void);   // rebuild LIBRARY tab after USB (re)mount; takes the LVGL lock
void      ui_trigger_library_refresh(void); // thread-safe trigger for main LVGL thread
void      ui_notify_usb_removed(void); // thread-safe notification from USB storage task
bool      ui_is_library_active(void);
esp_err_t ui_show_library(void);
esp_err_t ui_toggle_library_view(void);
esp_err_t ui_library_select_delta(int delta);
esp_err_t ui_library_load_selected(void);
esp_err_t ui_library_load_selected_for_deck(uint8_t deck);

// LVGL is not thread-safe. Any task other than the internal LVGL handler must
// wrap LVGL API calls in ui_lvgl_lock()/ui_lvgl_unlock(). No-ops on the PC sim.
void      ui_lvgl_lock(void);
void      ui_lvgl_unlock(void);

void ui_get_deck_track_info(uint8_t deck, char *out_title, size_t title_max, char *out_artist, size_t artist_max, uint16_t *out_bpm, uint32_t *out_duration_ms);
