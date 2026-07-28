#include "audio_recorder_stop_gate.h"
#include <stdio.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

int main(void)
{
    audio_recorder_stop_gate_t gate;
    audio_recorder_stop_gate_init(&gate);
    CHECK(audio_recorder_stop_gate_is_quiescent(&gate));
    CHECK(!audio_recorder_stop_gate_try_enter(&gate));
    CHECK(audio_recorder_stop_gate_open(&gate));
    CHECK(audio_recorder_stop_gate_try_enter(&gate));
    CHECK(audio_recorder_stop_gate_active(&gate) == 1u);

    /* STOP closes admission before waiting for the producer already in flight. */
    audio_recorder_stop_gate_close(&gate);
    CHECK(!audio_recorder_stop_gate_try_enter(&gate));
    CHECK(!audio_recorder_stop_gate_is_quiescent(&gate));
    audio_recorder_stop_gate_leave(&gate);
    CHECK(audio_recorder_stop_gate_is_quiescent(&gate));

    CHECK(audio_recorder_stop_gate_open(&gate));
    CHECK(audio_recorder_stop_gate_try_enter(&gate));
    audio_recorder_stop_gate_leave(&gate);
    audio_recorder_stop_gate_close(&gate);
    CHECK(audio_recorder_stop_gate_is_quiescent(&gate));

    if (failures == 0) puts("audio_recorder_stop_gate tests passed");
    return failures ? 1 : 0;
}
