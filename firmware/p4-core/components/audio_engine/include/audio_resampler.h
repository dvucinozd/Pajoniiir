#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"
#include "audio_dsp_features.h"

typedef bool (*audio_resampler_pop_fn)(void *ctx, audio_mixer_frame_t *out_frame);

typedef struct {
    bool antialias;
    audio_mixer_frame_t previous;
    audio_mixer_frame_t current;
    /* Q0.32 phase plus cached Q32 step. The input pitch is binary32, so every
     * supported value (0.01..16) is represented exactly after scaling by 2^32.
     * This keeps long-run consumption deterministic without double-precision
     * helpers in the ESP32-P4 per-sample path. */
    uint32_t phase_q32;
    uint32_t pitch_factor_bits;
    uint64_t step_q32;
#if AUDIO_ANTIALIAS_CACHE
    audio_mixer_frame_t history[1024];
    unsigned history_head;
    int filter_bank, previous_filter_bank;
    unsigned filter_fade;
#endif
} audio_resampler_state_t;

void audio_resampler_reset(audio_resampler_state_t *state);
audio_mixer_frame_t audio_resampler_next(audio_resampler_state_t *state,
                                         float pitch_factor,
                                         audio_resampler_pop_fn pop_source,
                                         void *source_ctx,
                                         uint32_t *out_consumed);
