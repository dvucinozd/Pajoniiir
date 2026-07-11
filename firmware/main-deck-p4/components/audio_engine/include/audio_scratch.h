#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_scratch_buffer.h"

/*
 * Scratch DSP engine (vinyl mode Phase 3/5). Turns jog motion into audible
 * turntable scratch by walking a fractional read head over the captured PCM
 * window (audio_scratch_buffer) and linearly interpolating, forward OR reverse.
 * Pure DSP: no ESP / RTOS deps, so it is exercised in isolation by host tests.
 * Driven from the real-time output task. See docs/VINYL_SCRATCH_PLAN.md.
 *
 * Coordinate: the read head is measured in `frames back from the newest
 * captured frame` (0 = newest, larger = older) as a float, so it needs no
 * sample rate and maps directly onto audio_scratch_buffer_read_frame_back().
 * `velocity` is the FORWARD advance in source frames per output sample: positive
 * moves toward newer audio (forward playback), negative toward older (reverse),
 * so each rendered sample does `head_back -= velocity`.
 *
 * Velocity model (Phase 5 — FIXED-WINDOW rate estimate). The FLX4 platter emits
 * many small (+-1) jog ticks at a high rate; the platter SPEED is carried by the
 * tick RATE. audio_scratch_jog just banks ticks (atomic). Every
 * `rate_window_samples` rendered samples the render loop snapshots the ticks
 * banked in that window and sets the target velocity to
 * `ticks x frames_per_tick / rate_window_samples` (a rate over a CONSTANT time
 * base). It then slews the playback velocity toward that target each sample.
 * A constant divisor is the whole point: the earlier "ticks since the last tick"
 * estimate divided by a noisy, bursty render-call count (render runs 256 samples
 * then blocks on I2S), so a tick landing just after a block gave a divisor near 1
 * and an 8x velocity spike, while a tick after an idle gap gave a huge divisor
 * and ~0 velocity — the head alternately froze and shot to the window edge.
 * Between windows with no ticks the velocity HOLDS (steady motion stays
 * continuous); only after `hold_windows` empty windows does the platter count as
 * stopped and the target slew to 0 (a still record is silent, not a tone).
 */
typedef struct {
    float    head_back;        /* fractional frames-back-from-newest read pos */
    float    velocity;         /* forward source-frames advanced per out sample */
    float    velocity_target;  /* estimator output the velocity slews toward */
    int32_t  pending_ticks;    /* jog ticks banked since the window close (atomic) */
    uint32_t window_pos;       /* rendered samples into the current rate window */
    uint32_t empty_windows;    /* consecutive tickless windows (for the stop) */
    float    frames_per_tick;  /* source frames one jog tick represents */
    uint32_t rate_window_samples; /* fixed time base for the rate estimate */
    float    slew_coef;        /* per-sample fraction moved toward the target */
    float    velocity_max;     /* clamp on |velocity| */
    uint32_t hold_windows;     /* empty windows before the platter counts stopped */
    bool     active;           /* seeded and rendering */
} audio_scratch_t;

/* Starting parameters (tune on hardware). Calibrated from a real FLX4 capture:
 * a moderate scratch emits ~250 +-1 ticks/s (one tick per ~4 ms). velocity =
 * tick_rate x frames_per_tick / sample_rate (independent of the window size), so
 * frames_per_tick 250 puts that ~250 tick/s scratch around 1.3x, a brisk
 * ~450 tick/s scratch ~2.3x, capped at 6x.
 *
 * The window MUST be a little longer than the tick interval, or most windows
 * hold zero ticks and the hold-bridge has to paper over the gaps — which also
 * lets a single stray tick from a hand resting on the platter drift the head for
 * the whole hold time (heard as a judder-in-place instead of a clean stop). A
 * 256-sample (~5 ms) window reliably contains a tick during real motion, so the
 * hold can be short: 3 windows (~16 ms) bridges the odd gap yet snaps to silence
 * the moment the hand stops. */
#define AUDIO_SCRATCH_DEFAULT_FRAMES_PER_TICK   250.0f
#define AUDIO_SCRATCH_DEFAULT_RATE_WINDOW       256u
#define AUDIO_SCRATCH_DEFAULT_SLEW_COEF         0.18f
#define AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX      6.0f
/* 3 windows x 256 samples ~= 16 ms with no ticks -> platter counts as stopped. */
#define AUDIO_SCRATCH_DEFAULT_HOLD_WINDOWS      3u
/* Below this |velocity| the platter counts as stopped -> silence. */
#define AUDIO_SCRATCH_SILENCE_VELOCITY          0.001f

/* Reset to defaults, inactive. */
void audio_scratch_init(audio_scratch_t *s);

/* Override the feel parameters. */
void audio_scratch_config(audio_scratch_t *s, float frames_per_tick,
                          uint32_t rate_window_samples, float slew_coef,
                          float velocity_max, uint32_t hold_windows);

/* Begin scratching from `head_back` frames before the newest (velocity 0). */
void audio_scratch_seed(audio_scratch_t *s, float head_back);

/* Stop scratching (renders silence until re-seeded). */
void audio_scratch_end(audio_scratch_t *s);

/* Add jog motion: `ticks` (signed) accumulate into the velocity estimator.
 * Callable from the control task while the output task renders (atomic add). */
void audio_scratch_jog(audio_scratch_t *s, int16_t ticks);

/* Current read position (frames back from newest), for the release handoff. */
float audio_scratch_head_back(const audio_scratch_t *s);

/* True while seeded/active. */
bool audio_scratch_is_active(const audio_scratch_t *s);

/* Render one stereo output frame from `buf` at the read head: advance the fixed-
 * window rate estimate, slew the velocity, advance the head. Returns true if
 * audio was produced; false (with silence) when inactive, stopped (velocity ~ 0),
 * the window is too small, or the head is clamped at a window edge. */
bool audio_scratch_render(audio_scratch_t *s, const audio_scratch_buffer_t *buf,
                          int16_t *out_left, int16_t *out_right);
