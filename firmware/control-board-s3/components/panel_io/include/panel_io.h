#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ─── Event types ─────────────────────────────────────────────────────────────

typedef enum {
    PANEL_EV_BUTTON = 0,  // id = button_id_t, value = 1 pressed / 0 released
    PANEL_EV_JOG,         // id = 0, value = signed delta (+ CW, - CCW)
    PANEL_EV_BROWSE,      // id = 1, value = signed delta (+ CW, - CCW)
    PANEL_EV_PITCH,       // id = 0, value = 0–16383 (14-bit scaled, calibrated)
} panel_event_type_t;

// Button IDs match the upstream XDJ100SX Teensy pin order for easy MIDI mapping.
typedef enum {
    BTN_EJECT = 0,
    BTN_TRACK_PREV,
    BTN_TRACK_NEXT,
    BTN_SEARCH_BACK,
    BTN_SEARCH_FWD,
    BTN_CUE,
    BTN_PLAY,
    BTN_PERF1,   // Jet
    BTN_PERF2,   // Zip
    BTN_PERF3,   // Wah
    BTN_HOLD,    // Hold (4th Digital Jog Break button)
    BTN_MODE,    // Time/Auto Cue
    BTN_MASTER_TEMPO,
    BTN_LOAD,    // Load selected library track
    BTN_COUNT,
} button_id_t;

typedef enum {
    LED_CUE = 0,
    LED_PLAY,
    LED_BEAT,
    LED_END,
    LED_PFL,
    LED_COUNT,
} led_id_t;

typedef struct {
    panel_event_type_t type;
    uint8_t id;
    int16_t value;
} panel_event_t;

// ─── Public API ──────────────────────────────────────────────────────────────

// Initialise all panel I/O subsystems and return the event queue.
// The queue holds up to 32 events; senders drop events on overflow.
esp_err_t panel_io_init(QueueHandle_t *event_queue_out);

// Drive a panel LED on or off immediately. Thread-safe.
void panel_led_set(led_id_t led, bool on);

// Make a LED blink at period_ms interval. Pass period_ms = 0 to stop blinking.
void panel_led_blink(led_id_t led, uint32_t period_ms);
