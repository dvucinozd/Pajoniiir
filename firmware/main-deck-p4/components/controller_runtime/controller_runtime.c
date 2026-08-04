/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_runtime.h"

#include <string.h>

static controller_runtime_config_t s_config;
static flx4_map_state_t s_map;
static bool s_initialized;
static bool s_connected;
static uint32_t s_midi_messages;
static uint32_t s_semantic_events;
static uint32_t s_unmapped_messages;
static uint32_t s_reconnect_snapshots;

static bool emit_snapshot_event(uint8_t type, uint8_t id, int16_t value,
                                void *ctx)
{
    (void)ctx;
    if (!s_config.event_cb) {
        return false;
    }
    const flx4_control_event_t event = {
        .type = type,
        .id = id,
        .value = value,
    };
    s_config.event_cb(&event, s_config.callback_ctx);
    (void)__atomic_add_fetch(&s_semantic_events, 1u, __ATOMIC_RELAXED);
    return true;
}

esp_err_t controller_runtime_init(const controller_runtime_config_t *config)
{
    if (!config || !config->event_cb) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    flx4_map_init(&s_map);
    s_connected = false;
    s_midi_messages = 0u;
    s_semantic_events = 0u;
    s_unmapped_messages = 0u;
    s_reconnect_snapshots = 0u;
    s_initialized = true;
    return ESP_OK;
}

bool controller_runtime_handle_midi(const usb_midi_message_t *message)
{
    if (!s_initialized || !message) {
        return false;
    }
    (void)__atomic_add_fetch(&s_midi_messages, 1u, __ATOMIC_RELAXED);

    flx4_control_event_t event;
    if (!flx4_map_message(&s_map, message, &event)) {
        (void)__atomic_add_fetch(&s_unmapped_messages, 1u,
                                 __ATOMIC_RELAXED);
        return false;
    }
    (void)__atomic_add_fetch(&s_semantic_events, 1u, __ATOMIC_RELAXED);
    s_config.event_cb(&event, s_config.callback_ctx);
    return true;
}

void controller_runtime_set_connected(bool connected)
{
    if (!s_initialized) {
        return;
    }
    const bool was_connected =
        __atomic_exchange_n(&s_connected, connected, __ATOMIC_ACQ_REL);
    if (connected && !was_connected) {
        (void)controller_runtime_emit_snapshot();
    }
}

size_t controller_runtime_emit_snapshot(void)
{
    if (!s_initialized || !s_config.event_cb) {
        return 0u;
    }
    const size_t count =
        flx4_map_emit_snapshot(&s_map, emit_snapshot_event, NULL);
    if (count > 0u) {
        (void)__atomic_add_fetch(&s_reconnect_snapshots, 1u,
                                 __ATOMIC_RELAXED);
    }
    return count;
}

void controller_runtime_get_diagnostics(
    controller_runtime_diagnostics_t *diag_out)
{
    if (!diag_out) {
        return;
    }
    *diag_out = (controller_runtime_diagnostics_t) {
        .midi_messages =
            __atomic_load_n(&s_midi_messages, __ATOMIC_ACQUIRE),
        .semantic_events =
            __atomic_load_n(&s_semantic_events, __ATOMIC_ACQUIRE),
        .unmapped_messages =
            __atomic_load_n(&s_unmapped_messages, __ATOMIC_ACQUIRE),
        .reconnect_snapshots =
            __atomic_load_n(&s_reconnect_snapshots, __ATOMIC_ACQUIRE),
        .connected = __atomic_load_n(&s_connected, __ATOMIC_ACQUIRE),
    };
}
