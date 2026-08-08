/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t dynamic_packets;
    uint32_t builtin_packets;
    uint32_t builtin_fallbacks;
    uint32_t unsupported;
    uint32_t send_failures;
} controller_led_runtime_diagnostics_t;

bool controller_led_runtime_build_packet(uint8_t led,
                                         uint8_t state,
                                         uint8_t deck,
                                         uint8_t packet[4]);
esp_err_t controller_led_runtime_send(uint8_t led,
                                      uint8_t state,
                                      uint8_t deck);
void controller_led_runtime_get_diagnostics(
    controller_led_runtime_diagnostics_t *diag_out);

#ifdef __cplusplus
}
#endif
