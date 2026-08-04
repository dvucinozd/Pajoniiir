/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_runtime.h"

#include "control_link.h"
#include "control_state_reconciler.h"
#include "controller_event_buffer.h"

static controller_runtime_config_t s_config;
static flx4_map_state_t s_map;
static controller_event_buffer_t s_buffer;
static control_held_state_reconciler_t s_held_states;
static bool s_initialized;
static bool s_connected;
static bool s_snapshot_pending;
static bool s_locked;
static uint32_t s_midi_messages;
static uint32_t s_mapped_messages;
static uint32_t s_semantic_events;
static uint32_t s_non_emitting_messages;
static uint32_t s_reconnect_snapshots;
static uint32_t s_held_reconciliations;
static uint32_t s_dispatch_calls;

static void runtime_lock(void)
{
    while (__atomic_test_and_set(&s_locked, __ATOMIC_ACQUIRE)) {
        /* Runtime APIs are task-context only; the protected sections are
         * bounded memory operations and never invoke application callbacks. */
    }
}

static void runtime_unlock(void)
{
    __atomic_clear(&s_locked, __ATOMIC_RELEASE);
}

static bool queue_snapshot_event(uint8_t type, uint8_t id, int16_t value,
                                 void *ctx)
{
    size_t *accepted = (size_t *)ctx;
    const flx4_control_event_t event = {
        .type = type,
        .id = id,
        .value = value,
    };
    if (!controller_event_buffer_push(&s_buffer, &event)) {
        return false;
    }
    (*accepted)++;
    return true;
}

static size_t held_dirty_count_locked(void)
{
    size_t count = 0u;
    for (size_t i = 0u; i < CONTROL_HELD_STATE_COUNT; ++i) {
        const control_held_state_slot_t *slot = &s_held_states.slots[i];
        if (slot->observed && slot->dirty) {
            count++;
        }
    }
    return count;
}

static size_t held_observed_count_locked(void)
{
    size_t count = 0u;
    for (size_t i = 0u; i < CONTROL_HELD_STATE_COUNT; ++i) {
        if (s_held_states.slots[i].observed) {
            count++;
        }
    }
    return count;
}

static void invalidate_held_schedule_locked(void)
{
    for (size_t i = 0u; i < CONTROL_HELD_STATE_COUNT; ++i) {
        control_held_state_slot_t *slot = &s_held_states.slots[i];
        if (!slot->observed) {
            continue;
        }
        slot->scheduled_valid = false;
        slot->dirty = true;
    }
}

static void prepare_snapshot_if_possible_locked(void)
{
    if (!s_snapshot_pending || s_buffer.count != 0u) {
        return;
    }

    size_t accepted = 0u;
    (void)flx4_map_emit_snapshot(&s_map, queue_snapshot_event, &accepted);
    const size_t held = held_observed_count_locked();
    s_snapshot_pending = false;
    if (accepted > 0u || held > 0u) {
        (void)__atomic_add_fetch(&s_reconnect_snapshots, 1u,
                                 __ATOMIC_RELAXED);
    }
}

esp_err_t controller_runtime_init(const controller_runtime_config_t *config)
{
    if (!config || !config->event_cb) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime_lock();
    s_config = *config;
    flx4_map_init(&s_map);
    controller_event_buffer_init(&s_buffer);
    control_held_state_reset(&s_held_states);
    s_connected = false;
    s_snapshot_pending = false;
    s_midi_messages = 0u;
    s_mapped_messages = 0u;
    s_semantic_events = 0u;
    s_non_emitting_messages = 0u;
    s_reconnect_snapshots = 0u;
    s_held_reconciliations = 0u;
    s_dispatch_calls = 0u;
    s_initialized = true;
    runtime_unlock();
    return ESP_OK;
}

bool controller_runtime_handle_midi(const usb_midi_message_t *message)
{
    if (!message || !__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return false;
    }
    (void)__atomic_add_fetch(&s_midi_messages, 1u, __ATOMIC_RELAXED);

    runtime_lock();
    flx4_control_event_t event;
    if (!flx4_map_message(&s_map, message, &event)) {
        runtime_unlock();
        (void)__atomic_add_fetch(&s_non_emitting_messages, 1u,
                                 __ATOMIC_RELAXED);
        return false;
    }

    (void)__atomic_add_fetch(&s_mapped_messages, 1u, __ATOMIC_RELAXED);
    const int held_key = control_held_state_key(event.id, event.value);
    if (event.type == CTRL_TYPE_BUTTON && held_key >= 0) {
        (void)control_held_state_observe(&s_held_states,
                                         event.id,
                                         event.value,
                                         0u);
        /* This queue item is only an ordering/wakeup token. If the bounded
         * buffer is full, the durable held-state slot remains dirty. */
        (void)controller_event_buffer_push(&s_buffer, &event);
    } else {
        (void)controller_event_buffer_push(&s_buffer, &event);
    }
    runtime_unlock();
    return true;
}

void controller_runtime_set_connected(bool connected)
{
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }

    runtime_lock();
    const bool was_connected = s_connected;
    s_connected = connected;
    if (connected && !was_connected) {
        invalidate_held_schedule_locked();
        s_snapshot_pending = true;
    } else if (!connected && was_connected) {
        control_held_state_release_all(&s_held_states, 0u);
    }
    runtime_unlock();
}

size_t controller_runtime_dispatch_pending(size_t max_events)
{
    if (max_events == 0u ||
        !__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return 0u;
    }

    (void)__atomic_add_fetch(&s_dispatch_calls, 1u, __ATOMIC_RELAXED);
    size_t dispatched = 0u;

    while (dispatched < max_events) {
        flx4_control_event_t event;
        int held_key = -1;
        bool have_held = false;
        bool have_buffered = false;

        runtime_lock();
        prepare_snapshot_if_possible_locked();

        size_t cursor = 0u;
        uint8_t held_id = 0u;
        int16_t held_value = 0;
        uint8_t held_sequence = 0u;
        have_held = control_held_state_next_dirty(
            &s_held_states,
            &cursor,
            &held_key,
            &held_id,
            &held_value,
            &held_sequence);
        (void)held_sequence;
        if (have_held) {
            event = (flx4_control_event_t) {
                .type = CTRL_TYPE_BUTTON,
                .id = held_id,
                .value = held_value,
            };
        } else {
            have_buffered = controller_event_buffer_pop(&s_buffer, &event);
        }
        runtime_unlock();

        if (have_held) {
            s_config.event_cb(&event, s_config.callback_ctx);
            runtime_lock();
            control_held_state_mark_scheduled(&s_held_states,
                                               held_key,
                                               event.value);
            runtime_unlock();
            (void)__atomic_add_fetch(&s_held_reconciliations, 1u,
                                     __ATOMIC_RELAXED);
            (void)__atomic_add_fetch(&s_semantic_events, 1u,
                                     __ATOMIC_RELAXED);
            dispatched++;
            continue;
        }

        if (!have_buffered) {
            break;
        }

        if (event.type == CTRL_TYPE_BUTTON &&
            control_held_state_key(event.id, event.value) >= 0) {
            /* Durable held-state reconciliation above already delivered the
             * current level. Discard this stale wake/order token. */
            continue;
        }

        s_config.event_cb(&event, s_config.callback_ctx);
        (void)__atomic_add_fetch(&s_semantic_events, 1u,
                                 __ATOMIC_RELAXED);
        dispatched++;
    }

    return dispatched;
}

size_t controller_runtime_emit_snapshot(void)
{
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return 0u;
    }
    runtime_lock();
    invalidate_held_schedule_locked();
    s_snapshot_pending = true;
    runtime_unlock();
    return controller_runtime_dispatch_pending(
        CONTROLLER_EVENT_BUFFER_CAPACITY + CONTROL_HELD_STATE_COUNT);
}

size_t controller_runtime_pending_count(void)
{
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return 0u;
    }
    runtime_lock();
    const size_t pending = s_buffer.count + held_dirty_count_locked() +
                           (s_snapshot_pending ? 1u : 0u);
    runtime_unlock();
    return pending;
}

void controller_runtime_get_diagnostics(
    controller_runtime_diagnostics_t *diag_out)
{
    if (!diag_out) {
        return;
    }

    runtime_lock();
    const size_t queued = s_buffer.count;
    const uint32_t coalesced = s_buffer.coalesced;
    const uint32_t dropped = s_buffer.dropped;
    const bool connected = s_connected;
    const bool snapshot_pending = s_snapshot_pending;
    runtime_unlock();

    *diag_out = (controller_runtime_diagnostics_t) {
        .midi_messages =
            __atomic_load_n(&s_midi_messages, __ATOMIC_ACQUIRE),
        .mapped_messages =
            __atomic_load_n(&s_mapped_messages, __ATOMIC_ACQUIRE),
        .semantic_events =
            __atomic_load_n(&s_semantic_events, __ATOMIC_ACQUIRE),
        .non_emitting_messages =
            __atomic_load_n(&s_non_emitting_messages, __ATOMIC_ACQUIRE),
        .reconnect_snapshots =
            __atomic_load_n(&s_reconnect_snapshots, __ATOMIC_ACQUIRE),
        .held_reconciliations =
            __atomic_load_n(&s_held_reconciliations, __ATOMIC_ACQUIRE),
        .dispatch_calls =
            __atomic_load_n(&s_dispatch_calls, __ATOMIC_ACQUIRE),
        .queue_coalesced = coalesced,
        .queue_dropped = dropped,
        .queued_events = queued,
        .connected = connected,
        .snapshot_pending = snapshot_pending,
    };
}
