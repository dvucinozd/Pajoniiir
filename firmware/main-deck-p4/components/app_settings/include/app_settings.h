#pragma once
//
// app_settings — persistent user settings stored in NVS.
//
// Survives reboots. Values are cached in RAM; setters write through to NVS
// immediately (small, infrequent writes). Firmware-only (uses NVS); the PC
// simulator should not link this.
//
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint8_t audio_out;      // 0 = onboard speaker, 1 = RCA line-out (bsp_audio_out_t)
    uint8_t backlight_pct;  // 10..100
    uint8_t time_remain;    // 0 = elapsed time, 1 = remaining time
    uint8_t link_mode;      // 0 = off, 1 = host USB, 2 = join player
    uint8_t cue_mode;       // 0 = stereo master, 1 = split mono (L=master, R=cue)
} app_settings_t;

// Initialise NVS and load saved settings (or sensible defaults if none stored).
esp_err_t      app_settings_init(void);

// Snapshot of the current settings.
app_settings_t app_settings_get(void);

// Update one setting and persist it to NVS.
void app_settings_set_audio_out(uint8_t out);
void app_settings_set_backlight(uint8_t pct);
void app_settings_set_time_remain(uint8_t remain);
void app_settings_set_link_mode(uint8_t mode);
void app_settings_set_cue_mode(uint8_t mode);
