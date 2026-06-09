#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"

#define AUDIO_PCM_RING_FRAMES 8192u

typedef struct {
    int16_t           frames[AUDIO_PCM_RING_FRAMES * 2u];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
} audio_pcm_ring_t;

void audio_pcm_ring_reset(audio_pcm_ring_t *ring);
uint32_t audio_pcm_ring_used(const audio_pcm_ring_t *ring);
uint32_t audio_pcm_ring_free(const audio_pcm_ring_t *ring);
bool audio_pcm_ring_push(audio_pcm_ring_t *ring, int16_t left, int16_t right);
bool audio_pcm_ring_pop(audio_pcm_ring_t *ring, audio_mixer_frame_t *out_frame);
