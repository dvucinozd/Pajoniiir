#include "library_validation_gate.h"

#include <stddef.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "media_io_gate.h"

#define VALIDATION_GATE_MIN_TIMEOUT_MS 5000u
#define VALIDATION_GATE_MAX_TIMEOUT_MS 60000u
#define VALIDATION_GATE_POLL_MS 10u

static atomic_uint_fast32_t s_state;
static atomic_uint_fast32_t s_sequence;
static atomic_uint_fast32_t s_timeout_ms;
#if defined(LIBRARY_VALIDATION_GATE_HOST_TEST)
static library_validation_gate_test_poll_hook_t s_test_poll_hook;
#endif

esp_err_t library_validation_gate_arm(uint32_t timeout_ms)
{
    if (timeout_ms < VALIDATION_GATE_MIN_TIMEOUT_MS ||
        timeout_ms > VALIDATION_GATE_MAX_TIMEOUT_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    uint_fast32_t state = atomic_load_explicit(&s_state, memory_order_acquire);
    if (state == LIBRARY_VALIDATION_GATE_ARMED ||
        state == LIBRARY_VALIDATION_GATE_HOLDING) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(&s_timeout_ms, timeout_ms, memory_order_relaxed);
    atomic_fetch_add_explicit(&s_sequence, 1u, memory_order_relaxed);
    atomic_store_explicit(&s_state, LIBRARY_VALIDATION_GATE_ARMED,
                          memory_order_release);
    return ESP_OK;
}

void library_validation_gate_cancel(void)
{
    uint_fast32_t state = atomic_load_explicit(&s_state, memory_order_acquire);
    if (state == LIBRARY_VALIDATION_GATE_ARMED ||
        state == LIBRARY_VALIDATION_GATE_HOLDING) {
        atomic_store_explicit(&s_state, LIBRARY_VALIDATION_GATE_CANCELED,
                              memory_order_release);
    }
}

bool library_validation_gate_checkpoint(void)
{
    uint_fast32_t expected = LIBRARY_VALIDATION_GATE_ARMED;
    if (!atomic_compare_exchange_strong_explicit(
            &s_state, &expected, LIBRARY_VALIDATION_GATE_HOLDING,
            memory_order_acq_rel, memory_order_acquire)) {
        return media_io_gate_is_available();
    }

    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS((uint32_t)atomic_load_explicit(
        &s_timeout_ms, memory_order_relaxed));
    for (;;) {
        if (!media_io_gate_is_available()) {
            atomic_store_explicit(&s_state,
                                  LIBRARY_VALIDATION_GATE_MEDIA_REMOVED,
                                  memory_order_release);
            return false;
        }
        uint_fast32_t state = atomic_load_explicit(&s_state,
                                                   memory_order_acquire);
        if (state != LIBRARY_VALIDATION_GATE_HOLDING) {
            return media_io_gate_is_available();
        }
        if ((TickType_t)(xTaskGetTickCount() - started) >= timeout) {
            atomic_store_explicit(&s_state,
                                  LIBRARY_VALIDATION_GATE_TIMED_OUT,
                                  memory_order_release);
            return media_io_gate_is_available();
        }
#if defined(LIBRARY_VALIDATION_GATE_HOST_TEST)
        if (s_test_poll_hook) s_test_poll_hook();
#endif
        vTaskDelay(pdMS_TO_TICKS(VALIDATION_GATE_POLL_MS));
    }
}

#if defined(LIBRARY_VALIDATION_GATE_HOST_TEST)
void library_validation_gate_test_reset(void)
{
    atomic_store(&s_state, LIBRARY_VALIDATION_GATE_IDLE);
    atomic_store(&s_sequence, 0u);
    atomic_store(&s_timeout_ms, 0u);
    s_test_poll_hook = NULL;
}

void library_validation_gate_test_set_poll_hook(
    library_validation_gate_test_poll_hook_t hook)
{
    s_test_poll_hook = hook;
}
#endif

void library_validation_gate_snapshot(library_validation_gate_snapshot_t *out)
{
    if (!out) return;
    *out = (library_validation_gate_snapshot_t) {
        .state = (library_validation_gate_state_t)atomic_load_explicit(
            &s_state, memory_order_acquire),
        .sequence = (uint32_t)atomic_load_explicit(&s_sequence,
                                                   memory_order_relaxed),
        .timeout_ms = (uint32_t)atomic_load_explicit(&s_timeout_ms,
                                                     memory_order_relaxed),
    };
}

const char *library_validation_gate_state_name(library_validation_gate_state_t state)
{
    switch (state) {
    case LIBRARY_VALIDATION_GATE_IDLE:          return "idle";
    case LIBRARY_VALIDATION_GATE_ARMED:         return "armed";
    case LIBRARY_VALIDATION_GATE_HOLDING:       return "holding";
    case LIBRARY_VALIDATION_GATE_MEDIA_REMOVED: return "media_removed";
    case LIBRARY_VALIDATION_GATE_TIMED_OUT:      return "timed_out";
    case LIBRARY_VALIDATION_GATE_CANCELED:       return "canceled";
    default:                                     return "invalid";
    }
}
