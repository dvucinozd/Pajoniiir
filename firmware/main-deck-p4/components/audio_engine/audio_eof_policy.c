#include "audio_eof_policy.h"

#include <stddef.h>

bool audio_eof_policy_should_finish(const audio_eof_policy_snapshot_t *snapshot)
{
    return snapshot != NULL &&
           snapshot->decoder_eof &&
           !snapshot->playback_finished &&
           snapshot->playing &&
           !snapshot->paused &&
           !snapshot->output_blocked &&
           snapshot->pending_frames == 0u;
}

bool audio_eof_policy_play_requires_rewind(bool playback_finished)
{
    return playback_finished;
}
