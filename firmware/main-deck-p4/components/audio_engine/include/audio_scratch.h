#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_scratch_buffer.h"

/*
 * Scratch DSP engine (vinyl mode Phase 3). Turns jog motion into audible
 * turntable scratch by walking a fractional read head over the captured PCM
 * window (audio_scratch_buffer) and linearly interpolating, forward OR reverse.
 * Pure DSP: no ESP / RTOS deps, so it is exercised in isolation by host tests.
 * Phase 4 will drive it from the real-time output task; Phase 3 only builds and
 * verifies the engine. See docs/VINYL_SCRATCH_PLAN.md.
 *
 * Coordinate: the read head is measured in `frames back from the newest
 * captured frame` (0 = newest, larger = older) as a float, so it needs no
 * sample rate and maps directly onto audio_scratch_buffer_read_frame_back().
 * `velocity` is the FORWARD advance in source frames per output sample: positive
 * moves toward newer audio (forward playback), negative toward older (reverse),
 * so each rendered sample does `head_back -= velocity`.
 *
 * Velocity model: a jog tick adds an impulse to the velocity (clamped to
 * ±velocity_max); velocity decays toward 0 every rendered sample, so a platter
 * held still (no ticks) coasts to a stop — and a stopped platter is silent
 * (like a still record), not a held tone.
 */
typedef struct {
    float head_back;         /* fractional frames-back-from-newest read pos */
    float velocity;          /* forward source-frames advanced per out sample */
    float velocity_per_tick; /* jog tick -> velocity impulse */
    float velocity_decay;    /* per-sample multiply toward 0 (coast/stop) */
    float velocity_max;      /* clamp on |velocity| */
    bool  active;            /* seeded and rendering */
} audio_scratch_t;

/* Sensible starting parameters (tune on hardware in Phase 5). */
#define AUDIO_SCRATCH_DEFAULT_VELOCITY_PER_TICK 0.35f
#define AUDIO_SCRATCH_DEFAULT_VELOCITY_DECAY    0.9950f
#define AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX      8.0f
/* Below this |velocity| the platter counts as stopped -> silence. */
#define AUDIO_SCRATCH_SILENCE_VELOCITY          0.001f

/* Reset to defaults, inactive. */
void audio_scratch_init(audio_scratch_t *s);

/* Override the feel parameters (any of them). */
void audio_scratch_config(audio_scratch_t *s, float velocity_per_tick,
                          float velocity_decay, float velocity_max);

/* Begin scratching from `head_back` frames before the newest (velocity 0). */
void audio_scratch_seed(audio_scratch_t *s, float head_back);

/* Stop scratching (renders silence until re-seeded). */
void audio_scratch_end(audio_scratch_t *s);

/* Add jog motion: `ticks` (signed) nudges the platter velocity. */
void audio_scratch_jog(audio_scratch_t *s, int16_t ticks);

/* Current read position (frames back from newest), for the Phase 4 handoff. */
float audio_scratch_head_back(const audio_scratch_t *s);

/* True while seeded/active. */
bool audio_scratch_is_active(const audio_scratch_t *s);

/* Render one stereo output frame from `buf` at the read head, advance the head
 * by the current velocity and decay the velocity. Returns true if audio was
 * produced; false (with silence) when inactive, stopped (velocity ~ 0), the
 * window is too small, or the head is clamped at a window edge. */
bool audio_scratch_render(audio_scratch_t *s, const audio_scratch_buffer_t *buf,
                          int16_t *out_left, int16_t *out_right);
