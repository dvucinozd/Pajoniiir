#include "midi_out_retry_state.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    midi_out_retry_state_t state;
    midi_out_retry_state_init(&state);
    assert(midi_out_retry_needs_payload(&state));

    midi_out_retry_payload_ready(&state);
    assert(!midi_out_retry_needs_payload(&state));

    /* A synchronous submit failure must retain the exact transfer buffer. */
    midi_out_retry_submit_result(&state, false);
    assert(!midi_out_retry_needs_payload(&state));
    assert(state.submit_retries == 1u);

    /* A failed completion is also retried in place, without queue dequeue. */
    midi_out_retry_submit_result(&state, true);
    midi_out_retry_complete(&state, MIDI_OUT_COMPLETION_RETRY);
    assert(!midi_out_retry_needs_payload(&state));
    assert(state.completion_retries == 1u);

    midi_out_retry_complete(&state, MIDI_OUT_COMPLETION_OK);
    assert(midi_out_retry_needs_payload(&state));

    midi_out_retry_payload_ready(&state);
    midi_out_retry_complete(&state, MIDI_OUT_COMPLETION_DISCONNECTED);
    assert(midi_out_retry_needs_payload(&state));
    assert(state.disconnect_drops == 1u);

    puts("midi_out_retry_state: all tests passed");
    return 0;
}
