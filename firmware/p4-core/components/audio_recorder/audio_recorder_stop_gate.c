#include "audio_recorder_stop_gate.h"

/* An unpaired leave() is a producer-ownership bug. Trap it where it happens on
 * firmware; the host suite deliberately drives the unpaired case to prove the
 * counter cannot wrap, so it uses the non-aborting recovery path instead. */
#if defined(AUDIO_RECORDER_STOP_GATE_HOST_TEST)
#define AUDIO_RECORDER_STOP_GATE_ASSERT(cond) ((void)0)
#else
#include "assert.h"
#define AUDIO_RECORDER_STOP_GATE_ASSERT(cond) assert(cond)
#endif

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
    /* Unconditional decrement, paired 1:1 with a successful try_enter().
     * The previous load-then-check-then-decrement was not atomic: two producers
     * leaving concurrently while the count was 1 could both observe "not zero"
     * and both decrement, wrapping the counter to UINT32_MAX and leaving the gate
     * permanently non-quiescent — STOP would then wait forever. A plain
     * fetch_sub cannot wrap for correctly paired callers, and for unpaired ones
     * it fails loudly in the assertion below instead of hanging the recorder. */
    uint32_t previous = __atomic_fetch_sub(&gate->active_producers, 1u, __ATOMIC_ACQ_REL);
    AUDIO_RECORDER_STOP_GATE_ASSERT(previous != 0u);
    if (previous == 0u) {
        /* Unpaired leave: restore the counter rather than leaving it wrapped. */
        __atomic_store_n(&gate->active_producers, 0u, __ATOMIC_RELEASE);
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
