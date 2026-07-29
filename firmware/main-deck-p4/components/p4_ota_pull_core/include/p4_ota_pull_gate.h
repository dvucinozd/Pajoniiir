#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t claimed;
} p4_ota_pull_gate_t;

static inline void p4_ota_pull_gate_init(p4_ota_pull_gate_t *gate)
{
    if (gate) {
        __atomic_store_n(&gate->claimed, 0u, __ATOMIC_RELEASE);
    }
}

static inline bool p4_ota_pull_gate_try_acquire(p4_ota_pull_gate_t *gate)
{
    if (!gate) {
        return false;
    }
    uint32_t expected = 0u;
    return __atomic_compare_exchange_n(&gate->claimed, &expected, 1u, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline void p4_ota_pull_gate_release(p4_ota_pull_gate_t *gate)
{
    if (gate) {
        __atomic_store_n(&gate->claimed, 0u, __ATOMIC_RELEASE);
    }
}

static inline bool p4_ota_pull_gate_is_claimed(const p4_ota_pull_gate_t *gate)
{
    return gate &&
           __atomic_load_n(&gate->claimed, __ATOMIC_ACQUIRE) != 0u;
}
