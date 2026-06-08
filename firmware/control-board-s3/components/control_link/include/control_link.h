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

// Initialise UART1 and start RX task.
// panel_event_queue: the queue returned by panel_io_init().
// Received LED/state commands from the P4 are applied to panel LEDs directly.
esp_err_t control_link_init(QueueHandle_t panel_event_queue);

// Serialise one panel event and transmit to ESP32-P4 over UART.
// Safe to call from any task.
void control_link_send_event(const panel_event_t *ev);

// Send a CTRL_TYPE_HEARTBEAT frame with the current uptime in seconds.
// Call periodically (e.g. every 5 s) so the P4 can detect S3 disconnects.
// Safe to call from any task.
void control_link_send_heartbeat(void);
