#pragma once

#include <stdint.h>

#define AUDIO_OUTPUT_BOOKKEEPING_DECKS 2u

/* The real-time output owner may fail to acquire the engine metadata lock at a
 * block boundary. Preserve accepted sink progress without blocking that task,
 * but discard progress from an older seek/load epoch before it can move the new
 * playhead. The state has one owner: the output task. */
typedef struct {
    uint64_t pending_frames[AUDIO_OUTPUT_BOOKKEEPING_DECKS];
    uint32_t pending_epoch[AUDIO_OUTPUT_BOOKKEEPING_DECKS];
} audio_output_bookkeeping_t;

void audio_output_bookkeeping_reset(audio_output_bookkeeping_t *state);

void audio_output_bookkeeping_note(audio_output_bookkeeping_t *state,
                                   uint8_t deck,
                                   uint32_t epoch,
                                   uint32_t consumed_frames);

uint64_t audio_output_bookkeeping_take(audio_output_bookkeeping_t *state,
                                       uint8_t deck,
                                       uint32_t current_epoch);
