#pragma once

// Onboard Seeed Studio XIAO ESP32S3 user status LED (GPIO21, active-low).
//
// Shows reduced one-LED link state:
//   on  — P4 UART link down, or FLX4 connected
//   off — P4 link up, FLX4 disconnected
//   tick — USB-MIDI input traffic

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
