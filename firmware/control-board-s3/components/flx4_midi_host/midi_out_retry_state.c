#include "midi_out_retry_state.h"

#include <string.h>

void midi_out_retry_state_init(midi_out_retry_state_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

bool midi_out_retry_needs_payload(const midi_out_retry_state_t *state)
{
    return !state || !state->payload_pending;
}

void midi_out_retry_payload_ready(midi_out_retry_state_t *state)
{
    if (state) state->payload_pending = true;
}

void midi_out_retry_submit_result(midi_out_retry_state_t *state, bool success)
{
    if (state && !success) state->submit_retries++;
}

void midi_out_retry_complete(midi_out_retry_state_t *state,
                             midi_out_completion_t completion)
{
    if (!state) return;

    switch (completion) {
    case MIDI_OUT_COMPLETION_OK:
        state->payload_pending = false;
        break;
    case MIDI_OUT_COMPLETION_RETRY:
        if (state->payload_pending) state->completion_retries++;
        break;
    case MIDI_OUT_COMPLETION_DISCONNECTED:
        if (state->payload_pending) state->disconnect_drops++;
        state->payload_pending = false;
        break;
    }
}
