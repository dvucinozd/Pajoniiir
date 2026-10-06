/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_midi_out_gate.h"

void controller_midi_out_gate_init(controller_midi_out_gate_t *gate)
{
    if (!gate) {
        return;
    }
    __atomic_store_n(&gate->accepting, false, __ATOMIC_RELEASE);
    __atomic_store_n(&gate->active_producers, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&gate->generation, 0u, __ATOMIC_RELEASE);
}

void controller_midi_out_gate_start(controller_midi_out_gate_t *gate)
{
    if (!gate) {
        return;
    }
    (void)__atomic_add_fetch(&gate->generation, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&gate->accepting, true, __ATOMIC_RELEASE);
}

bool controller_midi_out_gate_begin(controller_midi_out_gate_t *gate,
                                    uint32_t *generation_out)
{
    if (!gate || !__atomic_load_n(&gate->accepting, __ATOMIC_ACQUIRE)) {
        return false;
    }

    (void)__atomic_add_fetch(&gate->active_producers, 1u, __ATOMIC_ACQ_REL);
    if (!__atomic_load_n(&gate->accepting, __ATOMIC_ACQUIRE)) {
        (void)__atomic_sub_fetch(&gate->active_producers, 1u,
                                 __ATOMIC_ACQ_REL);
        return false;
    }
    if (generation_out) {
        *generation_out =
            __atomic_load_n(&gate->generation, __ATOMIC_ACQUIRE);
    }
    return true;
}

void controller_midi_out_gate_end(controller_midi_out_gate_t *gate)
{
    if (!gate) {
        return;
    }
    (void)__atomic_sub_fetch(&gate->active_producers, 1u, __ATOMIC_ACQ_REL);
}

void controller_midi_out_gate_stop(controller_midi_out_gate_t *gate)
{
    if (!gate) {
        return;
    }
    __atomic_store_n(&gate->accepting, false, __ATOMIC_RELEASE);
}

bool controller_midi_out_gate_is_accepting(
    const controller_midi_out_gate_t *gate)
{
    return gate && __atomic_load_n(&gate->accepting, __ATOMIC_ACQUIRE);
}

uint32_t controller_midi_out_gate_active_producers(
    const controller_midi_out_gate_t *gate)
{
    return gate ? __atomic_load_n(&gate->active_producers, __ATOMIC_ACQUIRE)
                : 0u;
}

uint32_t controller_midi_out_gate_generation(
    const controller_midi_out_gate_t *gate)
{
    return gate ? __atomic_load_n(&gate->generation, __ATOMIC_ACQUIRE) : 0u;
}
