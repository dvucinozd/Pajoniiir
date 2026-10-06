#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_mixer.h"

/* Gapless slip-censor reverse reader. The caller keeps the ordinary forward
 * renderer advancing, while this object owns only a reverse read head. */

#define AUDIO_CENSOR_EDGE_FADE_FRAMES 64u

typedef bool (*audio_censor_read_fn)(void *ctx,
                                     uint64_t sequence,
                                     audio_mixer_frame_t *out);

typedef struct {
    bool active;
    bool releasing;
    bool exhausted;
    uint64_t origin_seq;
    uint64_t distance_q32;
    uint64_t step_q32;
    uint32_t release_frames;
    uint32_t release_remaining;
    uint32_t edge_fade_remaining;
    uint32_t edge_hits;
    audio_mixer_frame_t last_reverse;
} audio_censor_t;

void audio_censor_init(audio_censor_t *censor);
bool audio_censor_begin(audio_censor_t *censor,
                        uint64_t origin_seq,
                        uint32_t source_sample_rate,
                        uint32_t output_sample_rate,
                        float speed_factor,
                        uint32_t release_frames);
void audio_censor_set_rate(audio_censor_t *censor,
                           uint32_t source_sample_rate,
                           uint32_t output_sample_rate,
                           float speed_factor);
void audio_censor_release(audio_censor_t *censor);
void audio_censor_reset(audio_censor_t *censor);
bool audio_censor_is_active(const audio_censor_t *censor);
bool audio_censor_render(audio_censor_t *censor,
                         audio_censor_read_fn read_frame,
                         void *read_ctx,
                         audio_mixer_frame_t *out_reverse,
                         float *out_reverse_gain);
