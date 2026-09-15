/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_runtime.h"

#include "control_link.h"
#include "control_state_reconciler.h"
#include "controller_event_buffer.h"
#include "controller_profile.h"
#include "controller_profile_runtime.h"

#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
static SemaphoreHandle_t s_runtime_mutex;
#else
#include <pthread.h>
static pthread_mutex_t s_runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static controller_runtime_config_t s_config;
static flx4_map_state_t s_map;
static controller_event_buffer_t s_buffer;
static flx4_control_event_t
    s_snapshot_events[CP_MAX_INPUTS];
static flx4_control_event_t s_retry_event;
static size_t s_snapshot_count;
static size_t s_snapshot_cursor;
static control_held_state_reconciler_t s_held_states;
static bool s_initialized;
static bool s_connected;
static uint32_t s_connection_generation;
static uint32_t s_connection_acknowledged;
static bool s_disconnect_pending;
static uint32_t s_disconnect_generation;
static bool s_builtin_flx4_enabled;
static bool s_snapshot_pending;
static bool s_retry_pending;
static bool s_dispatch_locked;
static uint32_t s_midi_messages;
static uint32_t s_mapped_messages;
static uint32_t s_semantic_events;
static uint32_t s_non_emitting_messages;
static uint32_t s_reconnect_snapshots;
static uint32_t s_held_reconciliations;
static uint32_t s_dispatch_calls;

static void runtime_lock(void)
{
#ifdef ESP_PLATFORM
    if (s_runtime_mutex) xSemaphoreTake(s_runtime_mutex, portMAX_DELAY);
#else
    pthread_mutex_lock(&s_runtime_mutex);
#endif
}

static void runtime_unlock(void)
{
#ifdef ESP_PLATFORM
    if (s_runtime_mutex) xSemaphoreGive(s_runtime_mutex);
#else
    pthread_mutex_unlock(&s_runtime_mutex);
#endif
}

typedef struct {
    flx4_control_event_t *events;
    size_t capacity;
    size_t count;
} snapshot_collector_t;

static bool collect_snapshot_event(uint8_t type, uint8_t id, int16_t value,
                                   void *ctx)
{
    snapshot_collector_t *collector = (snapshot_collector_t *)ctx;
    if (!collector || collector->count >= collector->capacity) {
        return false;
    }
    collector->events[collector->count++] = (flx4_control_event_t) {
        .type = type,
        .id = id,
        .value = value,
    };
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
    if (!s_snapshot_pending || s_buffer.count != 0u ||
        s_snapshot_cursor < s_snapshot_count) {
        return;
    }

    snapshot_collector_t collector = {
        .events = s_snapshot_events,
        .capacity = CP_MAX_INPUTS,
        .count = 0u,
    };
    if (controller_profile_runtime_active()) {
        (void)controller_profile_runtime_emit_snapshot(collect_snapshot_event,
                                                       &collector);
    } else if (__atomic_load_n(&s_builtin_flx4_enabled, __ATOMIC_ACQUIRE)) {
        (void)flx4_map_emit_snapshot(&s_map, collect_snapshot_event,
                                     &collector);
    }
    s_snapshot_count = collector.count;
    s_snapshot_cursor = 0u;
    const size_t held = held_observed_count_locked();
    s_snapshot_pending = false;
    if (s_snapshot_count > 0u || held > 0u) {
        (void)__atomic_add_fetch(&s_reconnect_snapshots, 1u,
                                 __ATOMIC_RELAXED);
    }
}

esp_err_t controller_runtime_init(const controller_runtime_config_t *config)
{
    if (!config || !config->event_cb) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    /* Bootstrap is the sole initializer, before USB/dispatch admission.
     * A mutex lets preempted owners inherit the waiting USB task's priority. */
    if (!s_runtime_mutex) s_runtime_mutex = xSemaphoreCreateMutex();
    if (!s_runtime_mutex) return ESP_ERR_NO_MEM;
#endif

    runtime_lock();
    s_config = *config;
    flx4_map_init(&s_map);
    controller_profile_runtime_init();
    controller_event_buffer_init(&s_buffer);
    control_held_state_reset(&s_held_states);
    s_connected = false;
    s_connection_generation = 0u;
    s_connection_acknowledged = 0u;
    s_disconnect_pending = false;
    s_disconnect_generation = 0u;
    __atomic_store_n(&s_builtin_flx4_enabled, false, __ATOMIC_RELEASE);
    s_snapshot_pending = false;
    s_retry_pending = false;
    __atomic_clear(&s_dispatch_locked, __ATOMIC_RELEASE);
    s_snapshot_count = 0u;
    s_snapshot_cursor = 0u;
    s_midi_messages = 0u;
    s_mapped_messages = 0u;
    s_semantic_events = 0u;
    s_non_emitting_messages = 0u;
    s_reconnect_snapshots = 0u;
    s_held_reconciliations = 0u;
    s_dispatch_calls = 0u;
    __atomic_store_n(&s_initialized, true, __ATOMIC_RELEASE);
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
    bool mapped;
    if (controller_profile_runtime_active()) {
        mapped = controller_profile_runtime_map(message->status,
                                                message->data1,
                                                message->data2,
                                                &event.type,
                                                &event.id,
                                                &event.value);
    } else if (__atomic_load_n(&s_builtin_flx4_enabled, __ATOMIC_ACQUIRE)) {
        mapped = flx4_map_message(&s_map, message, &event);
    } else {
        mapped = false;
    }
    if (!mapped) {
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
    if (connected != was_connected) s_connection_generation++;
    if (connected && !was_connected) {
        invalidate_held_schedule_locked();
        s_snapshot_pending = true;
    } else if (!connected && was_connected) {
        s_disconnect_pending = true;
        s_disconnect_generation = s_connection_generation;
        control_held_state_release_all(&s_held_states, 0u);
    }
    runtime_unlock();
}

void controller_runtime_set_builtin_flx4_enabled(bool enabled)
{
    __atomic_store_n(&s_builtin_flx4_enabled, enabled, __ATOMIC_RELEASE);
}

size_t controller_runtime_dispatch_pending(size_t max_events)
{
    if (max_events == 0u ||
        !__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return 0u;
    }
    if (__atomic_test_and_set(&s_dispatch_locked, __ATOMIC_ACQUIRE)) {
        return 0u;
    }

    (void)__atomic_add_fetch(&s_dispatch_calls, 1u, __ATOMIC_RELAXED);
    size_t dispatched = 0u;

    while (dispatched < max_events) {
        flx4_control_event_t event;
        int held_key = -1;
        bool have_held = false;
        bool have_retry = false;
        bool have_snapshot = false;
        bool have_buffered = false;
        uint32_t connection_generation = 0u;
        bool have_connection = false;
        uint32_t disconnect_generation = 0u;
        bool have_disconnect = false;
        bool captured_connected = false;

        runtime_lock();
        if (s_config.publish_connection_events &&
            (s_disconnect_pending || s_connection_generation != s_connection_acknowledged)) {
            connection_generation = s_connection_generation;
            disconnect_generation = s_disconnect_generation;
            have_disconnect = s_disconnect_pending;
            captured_connected = s_connected;
            have_connection = true;
            event = (flx4_control_event_t) {
                .type = CTRL_TYPE_STATE, .id = CTRL_ID_FLX4_CONNECTION,
                .value = !have_disconnect && captured_connected
                    ? CTRL_FLX4_CONNECTED : CTRL_FLX4_DISCONNECTED,
            };
        }
        if (have_connection) {
            runtime_unlock();
            if (s_config.event_cb(&event, s_config.callback_ctx) != ESP_OK) break;
            runtime_lock();
            /* A disconnect must release platters/loop adjustment even when a
             * reconnect arrives before queue space returns. Deliver it first,
             * then the latest connected state; never acknowledge a newer edge. */
            if (have_disconnect && s_disconnect_generation == disconnect_generation) {
                s_disconnect_pending = false;
            }
            if (!have_disconnect || !captured_connected) {
                s_connection_acknowledged = connection_generation;
            }
            runtime_unlock();
            __atomic_add_fetch(&s_semantic_events, 1u, __ATOMIC_RELAXED);
            dispatched++;
            continue;
        }
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
        } else if (s_retry_pending) {
            event = s_retry_event;
            have_retry = true;
        } else if (s_snapshot_cursor < s_snapshot_count) {
            event = s_snapshot_events[s_snapshot_cursor];
            have_snapshot = true;
        } else {
            have_buffered = controller_event_buffer_pop(&s_buffer, &event);
        }
        runtime_unlock();

        if (have_held) {
            if (s_config.event_cb(&event, s_config.callback_ctx) != ESP_OK) {
                /* Keep the durable level dirty until the downstream deck
                 * queue accepts it. */
                break;
            }
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

        if (have_retry) {
            if (s_config.event_cb(&event, s_config.callback_ctx) != ESP_OK) {
                break;
            }
            runtime_lock();
            s_retry_pending = false;
            runtime_unlock();
            (void)__atomic_add_fetch(&s_semantic_events, 1u,
                                     __ATOMIC_RELAXED);
            dispatched++;
            continue;
        }

        if (have_snapshot) {
            if (s_config.event_cb(&event, s_config.callback_ctx) != ESP_OK) {
                break;
            }
            runtime_lock();
            s_snapshot_cursor++;
            if (s_snapshot_cursor == s_snapshot_count) {
                s_snapshot_cursor = 0u;
                s_snapshot_count = 0u;
            }
            runtime_unlock();
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

        if (s_config.event_cb(&event, s_config.callback_ctx) != ESP_OK) {
            runtime_lock();
            s_retry_event = event;
            s_retry_pending = true;
            runtime_unlock();
            break;
        }
        (void)__atomic_add_fetch(&s_semantic_events, 1u,
                                 __ATOMIC_RELAXED);
        dispatched++;
    }

    __atomic_clear(&s_dispatch_locked, __ATOMIC_RELEASE);
    return dispatched;
}

void controller_runtime_request_snapshot(void)
{
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    runtime_lock();
    invalidate_held_schedule_locked();
    s_snapshot_pending = true;
    runtime_unlock();
}

size_t controller_runtime_emit_snapshot(void)
{
    controller_runtime_request_snapshot();
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return 0u;
    }
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
                           (s_snapshot_count - s_snapshot_cursor) +
                           (s_retry_pending ? 1u : 0u) +
                           (s_config.publish_connection_events &&
                            (s_disconnect_pending ||
                             s_connection_generation != s_connection_acknowledged) ? 1u : 0u) +
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
    const size_t snapshot_queued = s_snapshot_count - s_snapshot_cursor;
    const size_t retry_queued = s_retry_pending ? 1u : 0u;
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
        .queued_events = queued + snapshot_queued + retry_queued,
        .connected = connected,
        .snapshot_pending = snapshot_pending,
        .dynamic_profile_active = controller_profile_runtime_active(),
        .builtin_flx4_enabled =
            __atomic_load_n(&s_builtin_flx4_enabled, __ATOMIC_ACQUIRE),
    };
}
