#pragma once

// Onboard DevKitC-1 RGB status LED (WS2812 on GPIO48).
//
// Shows the link states at a glance, highest priority first:
//   amber  — P4 UART link down (no valid frame for 3 s; boot default until
//            the first frame — the P4 VU stream provides continuous traffic)
//   red    — P4 link up, FLX4 disconnected
//   green  — P4 link up, FLX4 connected
//   bright green flash — USB-MIDI input traffic while connected

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t status_led_init(void);
void status_led_set_connected(bool connected);
void status_led_notify_activity(void);
void status_led_notify_p4_frame(void);

#ifdef __cplusplus
}
#endif
