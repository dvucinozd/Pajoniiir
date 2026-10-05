#pragma once
#include "audio_pcm_timeline.h"

/* Decoder-owned, fixed-storage copy of the first second after manual LOOP IN.
 * No allocation or filesystem access. Session and IN identify its contents. */
typedef struct {
    int16_t *pcm;
    uint32_t capacity, frames, sample_rate, start_ms, session;
    bool valid;
} audio_loop_prefix_t;

typedef struct {
    uint32_t frames, period_frames, seek_ms;
} audio_loop_prefix_plan_t;

bool audio_loop_prefix_capture(audio_loop_prefix_t *p,
                               const audio_pcm_timeline_t *timeline,
                               uint64_t start_seq, uint32_t start_ms,
                               uint32_t sample_rate, uint32_t session);
audio_loop_prefix_plan_t audio_loop_prefix_plan(const audio_loop_prefix_t *p,
                                               uint32_t session,
                                               uint32_t start_ms,
                                               uint32_t end_ms);
bool audio_loop_prefix_frame(const audio_loop_prefix_t *p,
                             const audio_loop_prefix_plan_t *plan,
                             uint32_t index, audio_mixer_frame_t *out);
