/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Transport-neutral compiled controller-profile runtime.
 *
 * Holds an active S3CP v2 blob locally on the ESP32-P4 and maps raw MIDI,
 * reconnect snapshots and semantic LED frames without a UART round trip.
 * The S3CP name remains for file compatibility; the runtime is not tied to the
 * ESP32-S3 processor.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void controller_profile_runtime_init(void);
bool controller_profile_runtime_activate(const uint8_t *blob, size_t len,
                                         uint16_t vid, uint16_t pid);
void controller_profile_runtime_clear(void);
bool controller_profile_runtime_active(void);
bool controller_profile_runtime_map(uint8_t status, uint8_t data1,
                                    uint8_t data2, uint8_t *type,
                                    uint8_t *id, int16_t *value);
bool controller_profile_runtime_map_led(uint8_t led, uint8_t deck,
                                        uint8_t state, uint8_t packet[4]);

typedef bool (*controller_profile_runtime_emit_cb_t)(uint8_t type,
                                                     uint8_t id,
                                                     int16_t value,
                                                     void *ctx);
size_t controller_profile_runtime_emit_snapshot(
    controller_profile_runtime_emit_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
