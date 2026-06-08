#pragma once

// Internal API shared between panel_io.c, panel_encoder.c, panel_pitch.c, panel_leds.c.
// Not exposed to other components.

#include <stdint.h>
#include "esp_err.h"

// ─── Encoder (panel_encoder.c) ───────────────────────────────────────────────
esp_err_t panel_encoder_init(void);
// Returns accumulated encoder deltas since last call, resets internal counters.
void panel_encoder_read_deltas(int16_t *jog_delta, int16_t *browse_delta);

// ─── Pitch ADC (panel_pitch.c) ───────────────────────────────────────────────
esp_err_t panel_pitch_init(void);
// Returns 14-bit calibrated pitch value (0–16383). Returns UINT16_MAX when unchanged.
uint16_t panel_pitch_read_14bit(void);

// ─── LEDs (panel_leds.c) ─────────────────────────────────────────────────────
esp_err_t panel_leds_init(void);
// Advance blink timers; call every elapsed_ms milliseconds from scan task.
void panel_led_tick(uint32_t elapsed_ms);
