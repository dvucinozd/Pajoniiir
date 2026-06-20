#pragma once

#include <stdint.h>
#include <stdbool.h>
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
    LED_PFL,
    LED_COUNT,
} led_id_t;

// ─── DDJ-FLX4 deck-aware semantic IDs ────────────────────────────────────────

typedef enum {
    CTRL_DECK_1 = 0,
    CTRL_DECK_2 = 1,
    CTRL_DECK_NONE = 0xFF,
} ctrl_deck_t;

typedef enum {
    CTRL_DECK_CTL_PLAY = 0,
    CTRL_DECK_CTL_CUE,
    CTRL_DECK_CTL_JOG_SCRATCH,
    CTRL_DECK_CTL_JOG_BEND,
    CTRL_DECK_CTL_JOG_TOUCH,
    CTRL_DECK_CTL_TEMPO,
} ctrl_deck_control_t;

#define CTRL_NS_DECK1   0x10
#define CTRL_NS_DECK2   0x20
#define CTRL_NS_MIXER   0x30
#define CTRL_NS_BROWSER 0x40
#define CTRL_NS_SYSTEM  0x70

#define CTRL_ID_FLX4_CONNECTION (CTRL_NS_SYSTEM | 0x00)
#define CTRL_ID_SMART_CFX       (CTRL_NS_SYSTEM | 0x01)
#define CTRL_ID_SMART_FADER     (CTRL_NS_SYSTEM | 0x02)

typedef enum {
    CTRL_FLX4_DISCONNECTED = 0,
    CTRL_FLX4_CONNECTED = 1,
} ctrl_flx4_connection_t;

#define CTRL_ID_DECK1_PLAY        (CTRL_NS_DECK1 | CTRL_DECK_CTL_PLAY)
#define CTRL_ID_DECK1_CUE         (CTRL_NS_DECK1 | CTRL_DECK_CTL_CUE)
#define CTRL_ID_DECK1_JOG_SCRATCH (CTRL_NS_DECK1 | CTRL_DECK_CTL_JOG_SCRATCH)
#define CTRL_ID_DECK1_JOG_BEND    (CTRL_NS_DECK1 | CTRL_DECK_CTL_JOG_BEND)
#define CTRL_ID_DECK1_JOG_TOUCH   (CTRL_NS_DECK1 | CTRL_DECK_CTL_JOG_TOUCH)
#define CTRL_ID_DECK1_TEMPO       (CTRL_NS_DECK1 | CTRL_DECK_CTL_TEMPO)

#define CTRL_ID_DECK2_PLAY        (CTRL_NS_DECK2 | CTRL_DECK_CTL_PLAY)
#define CTRL_ID_DECK2_CUE         (CTRL_NS_DECK2 | CTRL_DECK_CTL_CUE)
#define CTRL_ID_DECK2_JOG_SCRATCH (CTRL_NS_DECK2 | CTRL_DECK_CTL_JOG_SCRATCH)
#define CTRL_ID_DECK2_JOG_BEND    (CTRL_NS_DECK2 | CTRL_DECK_CTL_JOG_BEND)
#define CTRL_ID_DECK2_JOG_TOUCH   (CTRL_NS_DECK2 | CTRL_DECK_CTL_JOG_TOUCH)
#define CTRL_ID_DECK2_TEMPO       (CTRL_NS_DECK2 | CTRL_DECK_CTL_TEMPO)

#define CTRL_ID_CH1_VOLUME        (CTRL_NS_MIXER | 0x00)
#define CTRL_ID_CH2_VOLUME        (CTRL_NS_MIXER | 0x01)
#define CTRL_ID_CROSSFADER        (CTRL_NS_MIXER | 0x02)
#define CTRL_ID_DECK1_PFL         (CTRL_NS_MIXER | 0x03)
#define CTRL_ID_DECK2_PFL         (CTRL_NS_MIXER | 0x04)

#define CTRL_ID_BROWSE_DELTA      (CTRL_NS_BROWSER | 0x00)
#define CTRL_ID_LOAD_DECK1        (CTRL_NS_BROWSER | 0x01)
#define CTRL_ID_LOAD_DECK2        (CTRL_NS_BROWSER | 0x02)
#define CTRL_ID_BROWSE_PRESS      (CTRL_NS_BROWSER | 0x03)

static inline bool control_link_id_is_deck(uint8_t id)
{
    return (id & 0xF0) == CTRL_NS_DECK1 || (id & 0xF0) == CTRL_NS_DECK2;
}

static inline uint8_t control_link_id_deck(uint8_t id)
{
    if ((id & 0xF0) == CTRL_NS_DECK1) return CTRL_DECK_1;
    if ((id & 0xF0) == CTRL_NS_DECK2) return CTRL_DECK_2;
    return CTRL_DECK_NONE;
}

static inline uint8_t control_link_id_control(uint8_t id)
{
    return control_link_id_is_deck(id) ? (uint8_t)(id & 0x0F) : id;
}

static inline bool control_link_id_is_deck_jog(uint8_t id)
{
    if (!control_link_id_is_deck(id)) return false;
    uint8_t ctl = control_link_id_control(id);
    return ctl == CTRL_DECK_CTL_JOG_SCRATCH || ctl == CTRL_DECK_CTL_JOG_BEND;
}

// ─── Parsed event (what deck_core receives) ───────────────────────────────────

typedef enum {
    CTRL_EV_BUTTON = 0,
    CTRL_EV_JOG,
    CTRL_EV_BROWSE,
    CTRL_EV_PITCH,
    CTRL_EV_HEARTBEAT,
    CTRL_EV_STATE,
} ctrl_event_type_t;

typedef struct {
    ctrl_event_type_t type;
    uint8_t           id;
    int16_t           value;
    uint8_t           seq;
    uint8_t           deck;     // CTRL_DECK_* for DDJ-FLX4 deck IDs
    uint8_t           control;  // low-nibble semantic control for deck IDs
} ctrl_event_t;

// ─── Public API ───────────────────────────────────────────────────────────────

// Initialise UART and start RX task.
// Parsed events are pushed onto ctrl_event_queue (created by deck_core_init).
esp_err_t control_link_init(QueueHandle_t ctrl_event_queue);

// Send LED command to S3. Thread-safe.
void control_link_send_led(led_id_t led, uint8_t state);  // state: 0 off / 1 on / 2 blink
void control_link_send_led_deck(led_id_t led, uint8_t state, uint8_t deck);
