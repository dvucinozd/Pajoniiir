#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Pitch calibration parameters (MVP: in-memory only, no NVS persistence).
typedef struct {
    uint16_t center_14bit;   // ADC raw center point, default 8192 (mid-range)
    uint16_t deadzone_14bit; // Counts around center treated as zero, default 200
    bool     invert;         // Invert pitch direction (XDJ100SX Mixxx mapping inverts)
} calibration_t;

esp_err_t calibration_init(void);

// Apply pitch calibration: raw 14-bit ADC value → calibrated 14-bit MIDI value.
// Returns 8192 when inside deadzone.
uint16_t calibration_apply_pitch(uint16_t raw_14bit);

const calibration_t *calibration_get(void);
void calibration_set(const calibration_t *cal);
