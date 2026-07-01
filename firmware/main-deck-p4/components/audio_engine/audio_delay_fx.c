#include "audio_delay_fx.h"

#include <string.h>

static int16_t clamp_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static int32_t q15_mul_i16(int16_t sample, uint16_t gain_q15)
{
    return ((int32_t)sample * (int32_t)gain_q15) >> 15;
}

void audio_delay_fx_init(audio_delay_fx_t *fx,
                         int16_t *left,
                         int16_t *right,
                         uint32_t capacity_frames,
                         uint32_t sample_rate)
{
    if (!fx) return;
    memset(fx, 0, sizeof(*fx));
    fx->left = left;
    fx->right = right;
    fx->capacity_frames = capacity_frames;
    fx->sample_rate = sample_rate;
    fx->allocated = left && right && capacity_frames > 1u && sample_rate > 0u;
    audio_delay_fx_reset(fx);
}

void audio_delay_fx_reset(audio_delay_fx_t *fx)
{
    if (!fx) return;
    fx->write_index = 0;
    if (fx->left && fx->capacity_frames > 0u) {
        memset(fx->left, 0, fx->capacity_frames * sizeof(fx->left[0]));
    }
    if (fx->right && fx->capacity_frames > 0u) {
        memset(fx->right, 0, fx->capacity_frames * sizeof(fx->right[0]));
    }
}

void audio_delay_fx_configure(audio_delay_fx_t *fx, const audio_delay_fx_config_t *config)
{
    if (!fx || !config) return;
    fx->config = *config;
    if (fx->config.wet_q15 > 32767u) fx->config.wet_q15 = 32767u;
    if (fx->config.feedback_q15 > 24576u) fx->config.feedback_q15 = 24576u;

    uint64_t frames = ((uint64_t)fx->sample_rate * (uint64_t)fx->config.delay_ms + 999u) / 1000u;
    if (frames < 1u) frames = 1u;
    if (fx->capacity_frames > 0u && frames >= fx->capacity_frames) {
        frames = fx->capacity_frames - 1u;
    }
    fx->delay_frames = (uint32_t)frames;
}

audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in)
{
    if (!fx || !fx->allocated || !fx->config.enabled || fx->delay_frames == 0u) {
        return in;
    }

    uint32_t read_index = (fx->write_index + fx->capacity_frames - fx->delay_frames) %
                          fx->capacity_frames;
    int16_t delayed_l = fx->left[read_index];
    int16_t delayed_r = fx->right[read_index];

    int32_t out_l = (int32_t)in.left + q15_mul_i16(delayed_l, fx->config.wet_q15);
    int32_t out_r = (int32_t)in.right + q15_mul_i16(delayed_r, fx->config.wet_q15);

    fx->left[fx->write_index] = clamp_i16((int32_t)in.left +
                                          q15_mul_i16(delayed_l, fx->config.feedback_q15));
    fx->right[fx->write_index] = clamp_i16((int32_t)in.right +
                                           q15_mul_i16(delayed_r, fx->config.feedback_q15));
    fx->write_index = (fx->write_index + 1u) % fx->capacity_frames;

    return (audio_mixer_frame_t) {
        .left = clamp_i16(out_l),
        .right = clamp_i16(out_r),
    };
}

bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx)
{
    return fx && fx->allocated;
}

uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx)
{
    return fx ? fx->config.delay_ms : 0u;
}
