/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "flx4_map.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*controller_runtime_event_cb_t)(
    const flx4_control_event_t *event, void *ctx);

typedef struct {
    controller_runtime_event_cb_t event_cb;
    void *callback_ctx;
} controller_runtime_config_t;

typedef struct {
    uint32_t midi_messages;
    uint32_t semantic_events;
    uint32_t unmapped_messages;
    uint32_t reconnect_snapshots;
    bool connected;
} controller_runtime_diagnostics_t;

esp_err_t controller_runtime_init(const controller_runtime_config_t *config);
bool controller_runtime_handle_midi(const usb_midi_message_t *message);
void controller_runtime_set_connected(bool connected);
size_t controller_runtime_emit_snapshot(void);
void controller_runtime_get_diagnostics(
    controller_runtime_diagnostics_t *diag_out);

#ifdef __cplusplus
}
#endif
