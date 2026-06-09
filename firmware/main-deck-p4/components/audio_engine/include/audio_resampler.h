#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"

typedef bool (*audio_resampler_pop_fn)(void *ctx, audio_mixer_frame_t *out_frame);

typedef struct {
    audio_mixer_frame_t previous;
    audio_mixer_frame_t current;
    double fraction;
} audio_resampler_state_t;

void audio_resampler_reset(audio_resampler_state_t *state);
audio_mixer_frame_t audio_resampler_next(audio_resampler_state_t *state,
                                         float pitch_factor,
                                         audio_resampler_pop_fn pop_source,
                                         void *source_ctx,
                                         uint32_t *out_consumed);
