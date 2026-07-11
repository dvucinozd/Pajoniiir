#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"
#define AUDIO_KEYLOCK_SYNTH_HOP 256u
typedef bool (*audio_keylock_read_fn)(void *, uint64_t, audio_mixer_frame_t *);
typedef struct {
    bool initialized, initial_half;
    uint32_t phase;
    double grain_a, grain_b, logical_seq;
    float tempo_factor, rate_ratio;
} audio_keylock_t;
void audio_keylock_reset(audio_keylock_t *, uint64_t);
void audio_keylock_configure(audio_keylock_t *, float, float);
bool audio_keylock_next(audio_keylock_t *, audio_keylock_read_fn, void *,
                        audio_mixer_frame_t *, uint32_t *, uint64_t *);
