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

typedef enum {
    AUDIO_OUTPUT_HEADPHONE_MASTER_MONO = 0,
    AUDIO_OUTPUT_HEADPHONE_CUE_MONO,
    AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO,
} audio_output_headphone_mode_t;

typedef struct {
    audio_mixer_frame_t master;
    audio_mixer_frame_t headphone;
} audio_output_mix_result_t;

audio_mixer_frame_t audio_output_mixer_next(const audio_output_mixer_deck_t *deck0,
                                            const audio_output_mixer_deck_t *deck1,
                                            uint32_t *out_deck0_consumed,
                                            uint32_t *out_deck1_consumed,
                                            audio_mixer_limiter_stats_t *limiter_stats);

audio_output_mix_result_t audio_output_mixer_next_full(const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       bool deck0_pfl,
                                                       bool deck1_pfl,
                                                       audio_output_headphone_mode_t headphone_mode,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats);
