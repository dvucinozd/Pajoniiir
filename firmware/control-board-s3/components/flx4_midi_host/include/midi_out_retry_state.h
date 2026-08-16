#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MIDI_OUT_COMPLETION_OK = 0,
    MIDI_OUT_COMPLETION_RETRY,
    MIDI_OUT_COMPLETION_DISCONNECTED,
} midi_out_completion_t;

typedef struct {
    bool payload_pending;
    uint32_t submit_retries;
    uint32_t completion_retries;
    uint32_t disconnect_drops;
} midi_out_retry_state_t;

void midi_out_retry_state_init(midi_out_retry_state_t *state);
bool midi_out_retry_needs_payload(const midi_out_retry_state_t *state);
void midi_out_retry_payload_ready(midi_out_retry_state_t *state);
void midi_out_retry_submit_result(midi_out_retry_state_t *state, bool success);
void midi_out_retry_complete(midi_out_retry_state_t *state,
                             midi_out_completion_t completion);
