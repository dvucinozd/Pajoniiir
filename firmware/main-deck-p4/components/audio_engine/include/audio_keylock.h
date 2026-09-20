#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"
#define AUDIO_KEYLOCK_SYNTH_HOP 256u
/* 2 * 192 search radius + 60 * 4 correlation span + interpolation endpoint.
 * Covers the largest supported source/output ratio (4) without stack growth. */
#define AUDIO_KEYLOCK_SEARCH_CACHE_FRAMES 640u
typedef bool (*audio_keylock_read_fn)(void *, uint64_t, audio_mixer_frame_t *);
typedef struct {
    bool initialized, initial_half;
    uint32_t phase;
    /* Float is hardware-accelerated on ESP32-P4; double is software-emulated.
     * Coordinates stay relative to origin_seq and are periodically rebased, so
     * float retains sub-frame precision even on hour-long tracks. */
    uint64_t origin_seq;
    uint64_t logical_seq;
    float grain_a, grain_b, logical_fraction;
    float tempo_factor, rate_ratio;
    /* Diagnostic bound for the most recent WSOLA search. Host tests use it to
     * prevent a regression to the exhaustive output-task hot path. */
    uint16_t last_search_candidates;
    /* Adjacent candidates revisit the same canonical PCM. Keep the bounded
     * per-search cache in persistent state, not on the 8 KiB output stack. */
    audio_mixer_frame_t search_frames[AUDIO_KEYLOCK_SEARCH_CACHE_FRAMES];
    uint8_t search_valid[AUDIO_KEYLOCK_SEARCH_CACHE_FRAMES];
} audio_keylock_t;
void audio_keylock_reset(audio_keylock_t *, uint64_t);
void audio_keylock_configure(audio_keylock_t *, float, float);
bool audio_keylock_next(audio_keylock_t *, audio_keylock_read_fn, void *,
                        audio_mixer_frame_t *, uint32_t *, uint64_t *);
