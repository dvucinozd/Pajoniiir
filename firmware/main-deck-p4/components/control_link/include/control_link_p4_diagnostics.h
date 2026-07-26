#pragma once

#include <stdbool.h>

#include "control_link_rx_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * P4-only receive diagnostics. Keep these declarations outside the shared
 * wire-protocol surface because the S3 does not consume P4 runtime telemetry.
 */

/* Observe authoritative S3 USB-controller CONNECTED / DISCONNECTED states.
 * The callback runs in the P4 control-link RX task. */
typedef void (*control_link_controller_state_cb_t)(bool connected);
void control_link_set_controller_state_cb(control_link_controller_state_cb_t cb);

/* Copy the receive-side 0xA5/0xA6 sequence and integrity counters. */
void control_link_get_rx_stats(control_link_rx_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
