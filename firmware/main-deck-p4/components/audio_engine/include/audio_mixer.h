#pragma once

#include <stdint.h>

#define AUDIO_MIXER_CONTROL_MAX    16383u
#define AUDIO_MIXER_CONTROL_CENTER 8192u

typedef struct {
    int16_t left;
    int16_t right;
} audio_mixer_frame_t;

typedef struct {
    uint32_t limited_samples;
    uint32_t positive_overloads;
    uint32_t negative_overloads;
    int32_t peak_input_abs;
} audio_mixer_limiter_stats_t;

float audio_mixer_fader_gain(uint16_t raw);
void audio_mixer_crossfader_gains(uint16_t raw, float *deck1_gain, float *deck2_gain);
int16_t audio_mixer_mix_sample(int16_t deck1,
                               int16_t deck2,
                               float deck1_gain,
                               float deck2_gain);
int16_t audio_mixer_limit_master_sample(int32_t mixed,
                                        audio_mixer_limiter_stats_t *stats);
audio_mixer_frame_t audio_mixer_apply_gain(audio_mixer_frame_t frame, float gain);
