#pragma once
/*
 * Master Tempo WSOLA v2 (JC1060, v326). Same contract as audio_keylock.h: one
 * output frame per call, read callback over the canonical timeline, consumed
 * source frames and the transport position (play_seq) out.
 *
 * v1 crossfades 256-frame grains linearly, aligns them on 2 samples within
 * +/-48 frames and reads fractional positions with linear interpolation (the
 * metallic tone and dull/grainy highs of the HIL). v2:
 *   - stretches at the source rate on whole frames only (no interpolation),
 *     then converts source -> output rate once with the 16-tap
 *     windowed-sinc resampler (bypassed at ratio 1);
 *   - SoundTouch-style segments: each hop plays one grain for
 *     AUDIO_KEYLOCK2_HOP frames (32 ms at 48 kHz), and only its first
 *     AUDIO_KEYLOCK2_OVERLAP frames (10.7 ms) crossfade from the previous
 *     grain's tail (smoothstep, amplitude-complementary). A permanent 50 %
 *     overlap (tried first) kept every frame a mix of two grains, which on a
 *     polyphonic mix is a standing comb filter;
 *   - grain alignment by normalised cross-correlation of the mono mix over
 *     the overlap within +/-AUDIO_KEYLOCK2_SEEK frames (10.7 ms: one period
 *     of 94 Hz, so bass lines can align too): 4x decimated coarse scan, then
 *     +/-3 frames at full rate. Host A/B on a three-note chord, energy off
 *     the partials at tempo 0.94/1.06: v1 -3 dB, +/-256 search -13..-19 dB,
 *     +/-512 -27..-28.5 dB (signalsmith-stretch, the FFT reference, -27..-32
 *     dB); wider or longer grains gained nothing;
 *   - the search for the next grain is spread over the current hop (a few
 *     reads or one or two correlation candidates per stretched frame), so no
 *     audio block pays for a whole search;
 *   - no added output latency: the timeline already holds the frames ahead.
 */
#include <stdbool.h>
#include <stdint.h>
#include "audio_keylock.h"
#include "wsola2_resampler.h"

#ifndef AUDIO_KEYLOCK2_HOP
#define AUDIO_KEYLOCK2_HOP 1536u                /* output frames per grain */
#endif
#ifndef AUDIO_KEYLOCK2_OVERLAP
#define AUDIO_KEYLOCK2_OVERLAP 512u             /* crossfade at the grain start */
#endif
#ifndef AUDIO_KEYLOCK2_SEEK
#define AUDIO_KEYLOCK2_SEEK 512u                /* search radius, frames */
#endif
#define AUDIO_KEYLOCK2_DECIMATION 4u
#define AUDIO_KEYLOCK2_CORR AUDIO_KEYLOCK2_OVERLAP   /* correlation length */
#define AUDIO_KEYLOCK2_CAND_FRAMES (2u * AUDIO_KEYLOCK2_SEEK + AUDIO_KEYLOCK2_CORR)

typedef struct {
    bool initialized;
    bool first_hop;          /* hop 0 after reset plays the start grain alone */
    float tempo_factor;
    float rate_ratio;
    /* Transport: exact integrated source position (tempo per stretched
     * frame), reported as play_seq. */
    uint64_t logical_seq;
    float logical_fraction;
    uint64_t floor_seq;      /* reset position: never read before it */
    /* Grains of the running hop, j = hop_pos: for j < OVERLAP out =
     * fade_out * S[grain_out + HOP + j] + fade_in * S[grain_in + j], then
     * S[grain_in + j] alone. */
    uint64_t grain_out;
    uint64_t grain_in;
    uint32_t hop_pos;
    /* Search for the grain of the next hop (staged over this hop). */
    uint32_t stage;
    uint32_t stage_pos;
    bool search_failed;
    uint64_t nominal;        /* predicted transport position at the next hop */
    uint64_t cand_first;     /* source frame of cand_mono[0] */
    uint32_t cand_count;     /* candidate frames actually wanted */
    uint64_t next_grain;
    float best_score;
    int32_t best_offset;     /* full-rate offset from cand_first */
    float energy;            /* running decimated candidate energy */
    int32_t fine_centre;     /* coarse winner, full-rate offset */
    /* Diagnostic (host tests): correlation candidates evaluated last hop. */
    uint16_t last_candidates;
    uint16_t candidates;
    int16_t ref_mono[AUDIO_KEYLOCK2_CORR];
    int16_t cand_mono[AUDIO_KEYLOCK2_CAND_FRAMES];
    float ref_dec[AUDIO_KEYLOCK2_CORR / AUDIO_KEYLOCK2_DECIMATION];
    float cand_dec[AUDIO_KEYLOCK2_CAND_FRAMES / AUDIO_KEYLOCK2_DECIMATION];
    /* Source -> output rate conversion of the stretched stream. */
    wsola2_resampler_state_t resampler;
    /* Read context of the running audio_keylock2_next() call. */
    audio_keylock_read_fn read;
    void *read_ctx;
    uint32_t pulled_consumed;
    bool pull_failed;
} audio_keylock2_t;

void audio_keylock2_reset(audio_keylock2_t *s, uint64_t start);
void audio_keylock2_configure(audio_keylock2_t *s, float tempo, float ratio);
bool audio_keylock2_next(audio_keylock2_t *s, audio_keylock_read_fn read, void *ctx,
                         audio_mixer_frame_t *out, uint32_t *consumed,
                         uint64_t *play_seq);
