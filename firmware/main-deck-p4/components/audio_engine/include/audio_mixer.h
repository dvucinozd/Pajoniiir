#pragma once

#include <stdint.h>

#define AUDIO_MIXER_CONTROL_MAX    16383u
#define AUDIO_MIXER_CONTROL_CENTER 8192u

typedef struct {
    int16_t left;
    int16_t right;
} audio_mixer_frame_t;

/* Wide internal DSP frame. PCM is converted to this format once after the
 * source/resampler, remains unclipped through channel DSP and summing, and is
 * converted back to int16 only at an output sink. */
typedef struct {
    float left;
    float right;
} audio_dsp_frame_t;

static inline audio_dsp_frame_t audio_mixer_dsp_from_pcm(
    audio_mixer_frame_t frame, float gain)
{
    if (!(gain > 0.0f)) gain = 0.0f;
    return (audio_dsp_frame_t) {
        .left = (float)frame.left * gain,
        .right = (float)frame.right * gain,
    };
}

static inline int16_t audio_mixer_pcm_sample_from_float(float sample)
{
    if (!(sample == sample)) return 0;
    if (sample > 32767.0f) return 32767;
    if (sample < -32768.0f) return -32768;
    return (int16_t)(sample >= 0.0f ? sample + 0.5f : sample - 0.5f);
}

static inline audio_mixer_frame_t audio_mixer_pcm_from_dsp(
    audio_dsp_frame_t frame)
{
    return (audio_mixer_frame_t) {
        .left = audio_mixer_pcm_sample_from_float(frame.left),
        .right = audio_mixer_pcm_sample_from_float(frame.right),
    };
}

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
int16_t audio_mixer_limit_master_float(float mixed,
                                       audio_mixer_limiter_stats_t *stats);
