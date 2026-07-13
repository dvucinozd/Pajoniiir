#include "audio_eof_policy.h"

#include <assert.h>
#include <stdio.h>

static audio_eof_policy_snapshot_t playing_at_eof(uint32_t pending_frames)
{
    return (audio_eof_policy_snapshot_t) {
        .decoder_eof = true,
        .playback_finished = false,
        .playing = true,
        .paused = false,
        .output_blocked = false,
        .pending_frames = pending_frames,
    };
}

int main(void)
{
    audio_eof_policy_snapshot_t state = playing_at_eof(96000u);
    assert(!audio_eof_policy_should_finish(&state));

    state.pending_frames = 0u;
    assert(audio_eof_policy_should_finish(&state));

    state.paused = true;
    assert(!audio_eof_policy_should_finish(&state));
    state.paused = false;

    state.output_blocked = true;
    assert(!audio_eof_policy_should_finish(&state));
    state.output_blocked = false;

    state.playing = false;
    assert(!audio_eof_policy_should_finish(&state));
    state.playing = true;

    state.playback_finished = true;
    assert(!audio_eof_policy_should_finish(&state));

    assert(!audio_eof_policy_play_requires_rewind(false));
    assert(audio_eof_policy_play_requires_rewind(true));
    assert(!audio_eof_policy_should_finish(NULL));

    puts("audio_eof_policy tests passed");
    return 0;
}
