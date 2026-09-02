/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile bool accepting;
    volatile uint32_t active_producers;
    volatile uint32_t generation;
} controller_midi_out_gate_t;

void controller_midi_out_gate_init(controller_midi_out_gate_t *gate);
void controller_midi_out_gate_start(controller_midi_out_gate_t *gate);
bool controller_midi_out_gate_begin(controller_midi_out_gate_t *gate,
                                    uint32_t *generation_out);
void controller_midi_out_gate_end(controller_midi_out_gate_t *gate);
void controller_midi_out_gate_stop(controller_midi_out_gate_t *gate);
bool controller_midi_out_gate_is_accepting(
    const controller_midi_out_gate_t *gate);
uint32_t controller_midi_out_gate_active_producers(
    const controller_midi_out_gate_t *gate);
uint32_t controller_midi_out_gate_generation(
    const controller_midi_out_gate_t *gate);

#ifdef __cplusplus
}
#endif
