#include "audio_filter.h"

#define AUDIO_FILTER_PI             3.14159265358979323846f
#define AUDIO_FILTER_CUTOFF_HZ      950.0f
#define AUDIO_FILTER_CENTER_DEAD_RAW 96u

static float one_pole_alpha(float cutoff_hz, uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0u) {
        sample_rate_hz = 44100u;
    }
    const float omega = 2.0f * AUDIO_FILTER_PI * cutoff_hz;
    return omega / (omega + (float)sample_rate_hz);
}

static int16_t clamp_i16_from_float(float sample)
{
    if (sample > 32767.0f) return 32767;
    if (sample < -32768.0f) return -32768;
    return (int16_t)(sample >= 0.0f ? sample + 0.5f : sample - 0.5f);
}

void audio_filter_reset(audio_filter_state_t *filter)
{
    if (!filter) return;
    filter->lp1[0] = 0.0f;
    filter->lp1[1] = 0.0f;
    filter->lp2[0] = 0.0f;
    filter->lp2[1] = 0.0f;
}

void audio_filter_init(audio_filter_state_t *filter, uint32_t sample_rate_hz)
{
    if (!filter) return;
    filter->raw = AUDIO_FILTER_RAW_CENTER;
    filter->alpha = one_pole_alpha(AUDIO_FILTER_CUTOFF_HZ, sample_rate_hz);
    audio_filter_reset(filter);
}

void audio_filter_set_raw(audio_filter_state_t *filter, uint16_t raw)
{
    if (!filter) return;
    if (raw > AUDIO_FILTER_RAW_MAX) {
        raw = AUDIO_FILTER_RAW_MAX;
    }
    filter->raw = raw;
}

uint16_t audio_filter_get_raw(const audio_filter_state_t *filter)
{
    return filter ? filter->raw : AUDIO_FILTER_RAW_CENTER;
}

static bool raw_is_center(uint16_t raw)
{
    uint16_t delta = raw > AUDIO_FILTER_RAW_CENTER
        ? raw - AUDIO_FILTER_RAW_CENTER
        : AUDIO_FILTER_RAW_CENTER - raw;
    return delta <= AUDIO_FILTER_CENTER_DEAD_RAW;
}

static float process_sample(audio_filter_state_t *filter, float sample, uint8_t channel)
{
    filter->lp1[channel] += filter->alpha * (sample - filter->lp1[channel]);
    filter->lp2[channel] += filter->alpha * (filter->lp1[channel] - filter->lp2[channel]);

    if (filter->raw < AUDIO_FILTER_RAW_CENTER) {
        return filter->lp2[channel];
    }
    return sample - filter->lp2[channel];
}

audio_mixer_frame_t audio_filter_process_frame(audio_filter_state_t *filter,
                                               bool enabled,
                                               audio_mixer_frame_t in)
{
    if (!filter || !enabled || raw_is_center(filter->raw)) {
        return in;
    }

    return (audio_mixer_frame_t) {
        .left = clamp_i16_from_float(process_sample(filter, (float)in.left, 0)),
        .right = clamp_i16_from_float(process_sample(filter, (float)in.right, 1)),
    };
}
