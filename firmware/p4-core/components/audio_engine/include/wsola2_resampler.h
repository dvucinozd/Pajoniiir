#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"

typedef bool (*wsola2_resampler_pop_fn)(void *ctx, audio_mixer_frame_t *out_frame);

/* v325 (JC1060 divergence from main-deck-p4): 16-tap windowed-sinc
 * interpolation instead of linear. The all-USB chain runs at 48 kHz, so every
 * 44.1 kHz track goes through this resampler even at pitch 0; linear
 * interpolation lost 1.5-4.5 dB at 10-17 kHz and left image spurs 13-23 dB
 * below the tone (the "dull and grainy" MP3s). Output lags the consumed
 * source by WSOLA2_RESAMPLER_TAPS / 2 frames (0.17 ms at 48 kHz) instead of 1. */
#define WSOLA2_RESAMPLER_TAPS 16u

typedef struct {
    /* Source history, doubled so the TAPS-frame window is always contiguous:
     * a new frame is stored at `write` and `write + TAPS`, then the window is
     * history[write .. write + TAPS - 1], oldest first. */
    audio_mixer_frame_t history[2u * WSOLA2_RESAMPLER_TAPS];
    uint32_t write;
    /* Q0.32 phase plus cached Q32 step. The input pitch is binary32, so every
     * supported value (0.01..16) is represented exactly after scaling by 2^32.
     * This keeps long-run consumption deterministic without double-precision
     * helpers in the ESP32-P4 per-sample path. */
    uint32_t phase_q32;
    uint32_t pitch_factor_bits;
    uint64_t step_q32;
} wsola2_resampler_state_t;

void wsola2_resampler_reset(wsola2_resampler_state_t *state);
audio_mixer_frame_t wsola2_resampler_next(wsola2_resampler_state_t *state,
                                         float pitch_factor,
                                         wsola2_resampler_pop_fn pop_source,
                                         void *source_ctx,
                                         uint32_t *out_consumed);
