#pragma once

#include "esp_err.h"
#include "panel_io.h"

// Inter-board UART protocol constants.
//
// Frame layout (7 bytes):
//   [0] 0xA5        start byte
//   [1] type        see CTRL_TYPE_* below
//   [2] id          button/encoder/led id
//   [3] val_lo      value LSB
//   [4] val_hi      value MSB
//   [5] seq         rolling sequence 0–255
//   [6] checksum    XOR of bytes [1]..[5]

#define CTRL_FRAME_LEN   7
#define CTRL_FRAME_START 0xA5

// S3 → P4 event types
#define CTRL_TYPE_BUTTON   0x01  // id=button_id, val=0/1
#define CTRL_TYPE_ENCODER  0x02  // id=0 jog / 1 browse, val=signed delta
#define CTRL_TYPE_PITCH    0x03  // id=0, val=0–16383
#define CTRL_TYPE_HEARTBEAT 0x04 // id=0, val=uptime seconds

// P4 → S3 command types
#define CTRL_TYPE_LED      0x81  // id=led_id, val=0 off / 1 on / 2 blink
#define CTRL_TYPE_STATE    0x82  // id=state_id, val=state value (reserved)

// DDJ-FLX4 deck-aware semantic IDs. Values must match the P4 control_link
// header; the wire frame stays unchanged.
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

// Initialise UART1 and start RX task.
// panel_event_queue: the queue returned by panel_io_init().
// Received LED/state commands from the P4 are applied to panel LEDs directly.
esp_err_t control_link_init(QueueHandle_t panel_event_queue);

// Send a deck-aware DDJ-FLX4 semantic event to P4 over the existing frame.
// Safe to call from any task.
esp_err_t control_link_send_semantic(uint8_t type, uint8_t id, int16_t value);

// Serialise one panel event and transmit to ESP32-P4 over UART.
// Safe to call from any task.
void control_link_send_event(const panel_event_t *ev);

// Send a CTRL_TYPE_HEARTBEAT frame with the current uptime in seconds.
// Call periodically (e.g. every 5 s) so the P4 can detect S3 disconnects.
// Safe to call from any task.
void control_link_send_heartbeat(void);
