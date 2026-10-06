#include "audio_output_bookkeeping.h"

#include <limits.h>
#include <string.h>

void audio_output_bookkeeping_reset(audio_output_bookkeeping_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

void audio_output_bookkeeping_note(audio_output_bookkeeping_t *state,
                                   uint8_t deck,
                                   uint32_t epoch,
                                   uint32_t consumed_frames)
{
    if (!state || deck >= AUDIO_OUTPUT_BOOKKEEPING_DECKS) return;

    if (state->pending_epoch[deck] != epoch) {
        state->pending_frames[deck] = 0u;
        state->pending_epoch[deck] = epoch;
    }

    uint64_t pending = state->pending_frames[deck];
    if (UINT64_MAX - pending < consumed_frames) {
        state->pending_frames[deck] = UINT64_MAX;
    } else {
        state->pending_frames[deck] = pending + consumed_frames;
    }
}

uint64_t audio_output_bookkeeping_take(audio_output_bookkeeping_t *state,
                                       uint8_t deck,
                                       uint32_t current_epoch)
{
    if (!state || deck >= AUDIO_OUTPUT_BOOKKEEPING_DECKS) return 0u;

    if (state->pending_epoch[deck] != current_epoch) {
        state->pending_frames[deck] = 0u;
        state->pending_epoch[deck] = current_epoch;
        return 0u;
    }

    uint64_t frames = state->pending_frames[deck];
    state->pending_frames[deck] = 0u;
    return frames;
}
