#include "audio_recorder_stop_gate.h"

void audio_recorder_stop_gate_init(audio_recorder_stop_gate_t *gate)
{
    if (!gate) return;
    __atomic_store_n(&gate->accepting, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gate->active_producers, 0u, __ATOMIC_RELEASE);
}

bool audio_recorder_stop_gate_open(audio_recorder_stop_gate_t *gate)
{
    if (!gate || __atomic_load_n(&gate->active_producers, __ATOMIC_ACQUIRE) != 0u) {
        return false;
    }
    __atomic_store_n(&gate->accepting, 1u, __ATOMIC_RELEASE);
    return true;
}

void audio_recorder_stop_gate_close(audio_recorder_stop_gate_t *gate)
{
    if (!gate) return;
    __atomic_store_n(&gate->accepting, 0u, __ATOMIC_RELEASE);
}

bool audio_recorder_stop_gate_try_enter(audio_recorder_stop_gate_t *gate)
{
    if (!gate || __atomic_load_n(&gate->accepting, __ATOMIC_ACQUIRE) == 0u) {
        return false;
    }
    __atomic_fetch_add(&gate->active_producers, 1u, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&gate->accepting, __ATOMIC_ACQUIRE) == 0u) {
        __atomic_fetch_sub(&gate->active_producers, 1u, __ATOMIC_ACQ_REL);
        return false;
    }
    return true;
}

void audio_recorder_stop_gate_leave(audio_recorder_stop_gate_t *gate)
{
    if (!gate) return;
    uint32_t active = __atomic_load_n(&gate->active_producers, __ATOMIC_ACQUIRE);
    if (active != 0u) {
        __atomic_fetch_sub(&gate->active_producers, 1u, __ATOMIC_ACQ_REL);
    }
}

bool audio_recorder_stop_gate_is_quiescent(const audio_recorder_stop_gate_t *gate)
{
    return !gate || __atomic_load_n(&gate->active_producers, __ATOMIC_ACQUIRE) == 0u;
}

uint32_t audio_recorder_stop_gate_active(const audio_recorder_stop_gate_t *gate)
{
    return gate ? __atomic_load_n(&gate->active_producers, __ATOMIC_ACQUIRE) : 0u;
}
