#include "audio_mixer.h"

static float clamp_gain(float gain)
{
    /* The negated comparison also rejects NaN. Letting NaN reach a float to
     * integer conversion is undefined behaviour and can poison the real-time
     * output path. */
    if (!(gain > 0.0f)) return 0.0f;
    if (gain > 1.0f) return 1.0f;
    return gain;
}

float audio_mixer_fader_gain(uint16_t raw)
{
    if (raw >= AUDIO_MIXER_CONTROL_MAX) return 1.0f;
    return (float)raw / (float)AUDIO_MIXER_CONTROL_MAX;
}

void audio_mixer_crossfader_gains(uint16_t raw, float *deck1_gain, float *deck2_gain)
{
    if (raw > AUDIO_MIXER_CONTROL_MAX) {
        raw = AUDIO_MIXER_CONTROL_MAX;
    }
    float d1 = 1.0f;
    float d2 = 1.0f;

    if (raw <= AUDIO_MIXER_CONTROL_CENTER) {
        d2 = (float)raw / (float)AUDIO_MIXER_CONTROL_CENTER;
    } else {
        d1 = (float)(AUDIO_MIXER_CONTROL_MAX - raw) /
             (float)(AUDIO_MIXER_CONTROL_MAX - AUDIO_MIXER_CONTROL_CENTER);
    }

    if (deck1_gain) *deck1_gain = clamp_gain(d1);
    if (deck2_gain) *deck2_gain = clamp_gain(d2);
}

int16_t audio_mixer_mix_sample(int16_t deck1,
                               int16_t deck2,
                               float deck1_gain,
                               float deck2_gain)
{
    float mixed = ((float)deck1 * clamp_gain(deck1_gain)) +
                  ((float)deck2 * clamp_gain(deck2_gain));

    if (mixed > 32767.0f) return 32767;
    if (mixed < -32768.0f) return -32768;
    return (int16_t)(mixed >= 0.0f ? mixed + 0.5f : mixed - 0.5f);
}

static int32_t sample_abs_i32_saturated(int32_t sample)
{
    if (sample == INT32_MIN) {
        return INT32_MAX;
    }
    return sample < 0 ? -sample : sample;
}

static int32_t soft_limit_abs_sample(int64_t abs_sample, int32_t knee, int32_t ceiling)
{
    if (abs_sample <= knee) {
        return (int32_t)abs_sample;
    }

    const float range = (float)(ceiling - knee);
    const float excess = (float)(abs_sample - knee);
    const float shaped = (float)knee + ((range * excess) / (excess + range));
    if (shaped >= (float)ceiling) return ceiling;
    return (int32_t)(shaped + 0.5f);
}

static int16_t limit_positive_sample(int32_t sample)
{
    return (int16_t)soft_limit_abs_sample(sample, 30000, 32767);
}

static int16_t limit_negative_sample(int32_t sample)
{
    int32_t magnitude = soft_limit_abs_sample(-(int64_t)sample, 30000, 32768);
    return magnitude >= 32768 ? INT16_MIN : (int16_t)-magnitude;
}

int16_t audio_mixer_limit_master_sample(int32_t mixed,
                                        audio_mixer_limiter_stats_t *stats)
{
    int32_t abs_mixed = sample_abs_i32_saturated(mixed);
    if (stats && abs_mixed > stats->peak_input_abs) {
        stats->peak_input_abs = abs_mixed;
    }

    if (mixed > 30000) {
        if (stats) {
            stats->limited_samples++;
            stats->positive_overloads++;
        }
        return limit_positive_sample(mixed);
    }
    if (mixed < -30000) {
        if (stats) {
            stats->limited_samples++;
            stats->negative_overloads++;
        }
        return limit_negative_sample(mixed);
    }
    return (int16_t)mixed;
}

audio_mixer_frame_t audio_mixer_apply_gain(audio_mixer_frame_t frame, float gain)
{
    return (audio_mixer_frame_t) {
        .left = audio_mixer_mix_sample(frame.left, 0, gain, 0.0f),
        .right = audio_mixer_mix_sample(frame.right, 0, gain, 0.0f),
    };
}
