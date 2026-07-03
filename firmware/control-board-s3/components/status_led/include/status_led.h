#pragma once

// Onboard DevKitC-1 RGB status LED (WS2812 on GPIO48).
//
// Shows the DDJ-FLX4 link state at a glance:
//   red    — FLX4 disconnected (boot default)
//   green  — FLX4 connected
//   bright green flash — USB-MIDI input traffic while connected

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t status_led_init(void);
void status_led_set_connected(bool connected);
void status_led_notify_activity(void);

#ifdef __cplusplus
}
#endif
