#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ─── Wire protocol (shared with ESP32-S3) ─────────────────────────────────────
//
// Frame layout (7 bytes):
//   [0] 0xA5        start byte
//   [1] type        CTRL_TYPE_* below
//   [2] id          button / encoder / led id
//   [3] val_lo      value LSB
//   [4] val_hi      value MSB
//   [5] seq         rolling sequence 0–255
//   [6] checksum    XOR of bytes [1]..[5]

#define CTRL_FRAME_LEN    7
#define CTRL_FRAME_START  0xA5

// S3 → P4
#define CTRL_TYPE_BUTTON     0x01  // id = button_id_t,  val = 0 released / 1 pressed
#define CTRL_TYPE_ENCODER    0x02  // id = 0 jog,        val = signed delta
#define CTRL_TYPE_PITCH      0x03  // id = 0,            val = 0–16383
#define CTRL_TYPE_HEARTBEAT  0x04  // id = 0,            val = firmware uptime s

// P4 → S3
#define CTRL_TYPE_LED        0x81  // id = led_id_t, val = 0 off / 1 on / 2 blink
#define CTRL_TYPE_STATE      0x82  // reserved

// ─── Button IDs (must match ESP32-S3 button_id_t) ────────────────────────────

typedef enum {
    BTN_EJECT = 0,
    BTN_TRACK_PREV,
    BTN_TRACK_NEXT,
    BTN_SEARCH_BACK,
    BTN_SEARCH_FWD,
    BTN_CUE,
    BTN_PLAY,
    BTN_PERF1,         // Jet
    BTN_PERF2,         // Zip
    BTN_PERF3,         // Wah
    BTN_HOLD,          // Hold (4th Digital Jog Break)
    BTN_MODE,          // Time / Auto Cue
    BTN_MASTER_TEMPO,
    BTN_LOAD,          // Load selected library track
    BTN_COUNT,
} button_id_t;

// ─── LED IDs (must match ESP32-S3 led_id_t) ──────────────────────────────────

typedef enum {
    LED_CUE = 0,
    LED_PLAY,
    LED_BEAT,
    LED_END,
    LED_COUNT,
} led_id_t;

// ─── Parsed event (what deck_core receives) ───────────────────────────────────

typedef enum {
    CTRL_EV_BUTTON = 0,
    CTRL_EV_JOG,
    CTRL_EV_BROWSE,
    CTRL_EV_PITCH,
    CTRL_EV_HEARTBEAT,
} ctrl_event_type_t;

typedef struct {
    ctrl_event_type_t type;
    uint8_t           id;
    int16_t           value;
    uint8_t           seq;
} ctrl_event_t;

// ─── Public API ───────────────────────────────────────────────────────────────

// Initialise UART and start RX task.
// Parsed events are pushed onto ctrl_event_queue (created by deck_core_init).
esp_err_t control_link_init(QueueHandle_t ctrl_event_queue);

// Send LED command to S3. Thread-safe.
void control_link_send_led(led_id_t led, uint8_t state);  // state: 0 off / 1 on / 2 blink
