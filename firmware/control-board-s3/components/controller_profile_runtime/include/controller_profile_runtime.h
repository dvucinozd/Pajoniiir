#pragma once

/* S3 dynamic controller-profile runtime.
 *
 * Holds the active compiled profile received from the P4 (S3CP blob validated
 * by the control-link transfer) and maps raw MIDI through it instead of the
 * built-in flx4_map. When no profile is active, callers fall back to the
 * built-in FLX4 map. Thread-safe: activation runs on the control-link RX task
 * while mapping runs on the MIDI callback task.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the internal lock. Call once at startup before any activate/map. */
void controller_profile_runtime_init(void);

/* Parse and install `blob` as the active profile. Returns true on success
 * (ACK), false when the blob is not a valid S3CP profile (NACK PARSE).
 * `blob == NULL` clears the active profile (used by PROFILE_CLEAR). */
bool controller_profile_runtime_activate(const uint8_t *blob, size_t len,
                                         uint16_t vid, uint16_t pid);

/* Drop the active profile (fall back to the built-in map). */
void controller_profile_runtime_clear(void);

/* True when a dynamic profile is installed and should be used for mapping. */
bool controller_profile_runtime_active(void);

/* Map one 3-byte MIDI message through the active profile. Returns true and
 * fills the type/id/value out-params (control_link semantic event) when the
 * message produced an event; false when there is no active profile or no
 * match. */
bool controller_profile_runtime_map(uint8_t status, uint8_t data1, uint8_t data2,
                                     uint8_t *type, uint8_t *id, int16_t *value);

/* Map a P4 LED frame (led id + deck + state) to a 4-byte USB-MIDI packet for
 * the connected controller, using the active profile's output table. Fills
 * packet[0]=CIN (MIDI status nibble), packet[1..3]=status/data1/data2.
 * Returns false when no profile is active or the profile has no mapping for
 * (led, deck) -- the caller then falls back to the built-in FLX4 LED map. */
bool controller_profile_runtime_map_led(uint8_t led, uint8_t deck, uint8_t state,
                                        uint8_t packet[4]);

/* Re-emit the replay-flagged absolute controls of the active profile (input
 * snapshot replay after reconnect). Returns the number emitted; 0 when no
 * profile is active. */
typedef bool (*controller_profile_runtime_emit_cb_t)(uint8_t type, uint8_t id,
                                                     int16_t value, void *ctx);
size_t controller_profile_runtime_emit_snapshot(controller_profile_runtime_emit_cb_t cb,
                                                void *ctx);

#ifdef __cplusplus
}
#endif
