#include "controller_midi_out_gate.h"

#include <stdio.h>
#include <stdlib.h>

static int assertions;

#define CHECK(expr) do { \
    assertions++; \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

int main(void)
{
    controller_midi_out_gate_t gate;
    controller_midi_out_gate_init(&gate);
    CHECK(!controller_midi_out_gate_is_accepting(&gate));
    CHECK(controller_midi_out_gate_active_producers(&gate) == 0u);
    CHECK(controller_midi_out_gate_generation(&gate) == 0u);

    uint32_t generation = 99u;
    CHECK(!controller_midi_out_gate_begin(&gate, &generation));

    controller_midi_out_gate_start(&gate);
    CHECK(controller_midi_out_gate_is_accepting(&gate));
    CHECK(controller_midi_out_gate_generation(&gate) == 1u);
    CHECK(controller_midi_out_gate_begin(&gate, &generation));
    CHECK(generation == 1u);
    CHECK(controller_midi_out_gate_active_producers(&gate) == 1u);

    controller_midi_out_gate_stop(&gate);
    CHECK(!controller_midi_out_gate_is_accepting(&gate));
    CHECK(!controller_midi_out_gate_begin(&gate, NULL));
    CHECK(controller_midi_out_gate_active_producers(&gate) == 1u);
    controller_midi_out_gate_end(&gate);
    CHECK(controller_midi_out_gate_active_producers(&gate) == 0u);

    controller_midi_out_gate_start(&gate);
    CHECK(controller_midi_out_gate_generation(&gate) == 2u);
    CHECK(controller_midi_out_gate_begin(&gate, &generation));
    CHECK(generation == 2u);
    controller_midi_out_gate_end(&gate);

    controller_midi_out_gate_stop(&gate);
    CHECK(!controller_midi_out_gate_begin(NULL, NULL));
    CHECK(controller_midi_out_gate_active_producers(NULL) == 0u);
    CHECK(controller_midi_out_gate_generation(NULL) == 0u);

    printf("controller MIDI OUT gate: %d assertions passed\n", assertions);
    return 0;
}
