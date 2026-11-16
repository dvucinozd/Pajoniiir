#pragma once
/*
 * v337 (JC1060): 2:1 half-band decimation of stereo int16 PCM, for 88.2 /
 * 96 kHz sources (once) and 176.4 / 192 kHz ones (twice) before they enter
 * the deck timeline. Without it a 96 kHz track filled the 4 s / 48 kHz store
 * with its 2 s forward runway alone (no history, no scratch) and the 2:1 sinc
 * resampler, whose cutoff follows the source rate, folded 24-43 kHz content
 * into the audible band. Decode task only; no allocation.
 *
 * 63-tap half-band (audio_decim2_table.h): flat within 0.0005 dB to 0.205 of
 * the input rate, >= 85 dB down from 0.295. Group delay 31 input frames
 * (15.5 output frames, 0.32 ms at 48 kHz), not compensated, like the v325
 * resampler's 8 frames.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_DECIM2_HIST 64u   /* >= taps, power of two */

typedef struct {
    float    hist[AUDIO_DECIM2_HIST][2];
    uint32_t pos;               /* next write slot */
    bool     odd;               /* an input frame is waiting for its pair */
} audio_decim2_t;

void audio_decim2_reset(audio_decim2_t *d);
/* n interleaved stereo frames in, one frame out per two in; returns the
 * frames written. out may be in (the output never overtakes the input). */
size_t audio_decim2_process(audio_decim2_t *d, const int16_t *in, size_t n, int16_t *out);
/* Stages for a source rate: 0 below 64 kHz, 1 up to 128 kHz, 2 up to 256
 * kHz, -1 above (refused). */
int audio_decim2_stages(uint32_t source_rate);
