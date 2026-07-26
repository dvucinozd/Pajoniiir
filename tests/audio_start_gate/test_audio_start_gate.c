#include "audio_start_gate.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(!audio_start_gate_ready(0u, 512u, false));
    assert(!audio_start_gate_ready(511u, 512u, false));
    assert(audio_start_gate_ready(512u, 512u, false));
    assert(audio_start_gate_ready(4096u, 512u, false));

    /* EOF releases a real short tail, but never an empty source. */
    assert(audio_start_gate_ready(1u, 512u, true));
    assert(!audio_start_gate_ready(0u, 512u, true));
    assert(audio_start_gate_ready(0u, 0u, false));

    puts("audio_start_gate tests passed");
    return 0;
}
