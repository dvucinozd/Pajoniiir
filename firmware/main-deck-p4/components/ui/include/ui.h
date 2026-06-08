#pragma once

// LVGL DJ UI — landscape 800x480 after 90° rotation.
//
// Tabs (matching upstream XDJ100SX skin):
//   overview | library | hotcues | beatloop | beatjump | keyshift | settings

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ui_init(void);
void      ui_update(void);   // call from LVGL timer task with current deck_state
void      ui_refresh_library(void);   // rebuild LIBRARY tab after USB (re)mount; takes the LVGL lock
void      ui_trigger_library_refresh(void); // thread-safe trigger for main LVGL thread
void      ui_notify_usb_removed(void); // thread-safe notification from USB storage task
bool      ui_is_library_active(void);
esp_err_t ui_library_select_delta(int delta);
esp_err_t ui_library_load_selected(void);

// LVGL is not thread-safe. Any task other than the internal LVGL handler must
// wrap LVGL API calls in ui_lvgl_lock()/ui_lvgl_unlock(). No-ops on the PC sim.
void      ui_lvgl_lock(void);
void      ui_lvgl_unlock(void);
