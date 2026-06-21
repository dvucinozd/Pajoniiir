#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"
#include "audio_resampler.h"

typedef struct {
    bool active;
    float pitch_factor;
    float gain;
    audio_resampler_state_t *resampler;
    audio_resampler_pop_fn pop_source;
    void *source_ctx;
} audio_output_mixer_deck_t;

audio_mixer_frame_t audio_output_mixer_next(const audio_output_mixer_deck_t *deck0,
                                            const audio_output_mixer_deck_t *deck1,
                                            uint32_t *out_deck0_consumed,
                                            uint32_t *out_deck1_consumed,
                                            audio_mixer_limiter_stats_t *limiter_stats);
