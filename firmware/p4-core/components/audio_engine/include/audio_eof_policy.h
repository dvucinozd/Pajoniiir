#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool decoder_eof;
    bool playback_finished;
    bool playing;
    bool paused;
    bool output_blocked;
    uint32_t pending_frames;
} audio_eof_policy_snapshot_t;

bool audio_eof_policy_should_finish(const audio_eof_policy_snapshot_t *snapshot);
bool audio_eof_policy_play_requires_rewind(bool playback_finished);

/* An empty source after the decoder has reached EOF is the expected final
 * partial output block, not a starvation fault. */
bool audio_eof_policy_should_count_empty_source(bool decoder_eof);
