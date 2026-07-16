#include "audio_delay_fx.h"

#include <string.h>

/* 2*pi*4.5 kHz — cutoff of the one-pole damping filter in the feedback path.
 * Each repeat passes through it once, so the echo darkens per generation
 * (tape-style) instead of repeating at full bandwidth. */
#define AUDIO_DELAY_FX_DAMP_OMEGA 28274u
/* How long the tail keeps ringing after the effect is switched off. */
#define AUDIO_DELAY_FX_TAIL_SECONDS 2u
/* Gain ramps move 1/64th of the remaining distance per frame (~1.5 ms
 * time constant at 44.1 kHz). */
#define AUDIO_DELAY_FX_SMOOTH_SHIFT 6

static int16_t clamp_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static int32_t q15_mul_i32(int32_t sample, uint16_t gain_q15)
{
    return (sample * (int32_t)gain_q15) >> 15;
}

static uint16_t smooth_q15(uint16_t current, uint16_t target)
{
    int32_t delta = (int32_t)target - (int32_t)current;
    int32_t step = delta / (1 << AUDIO_DELAY_FX_SMOOTH_SHIFT);
    if (step == 0 && delta != 0) {
        step = delta > 0 ? 1 : -1;
    }
    return (uint16_t)((int32_t)current + step);
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
    fx->fb_lp[0] = 0;
    fx->fb_lp[1] = 0;
    fx->wet_cur_q15 = 0;
    fx->feedback_cur_q15 = 0;
    fx->tail_frames_remaining = 0;
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
    bool was_enabled = fx->config.enabled;
    audio_delay_fx_config_t next = *config;
    if (next.wet_q15 > 32767u) next.wet_q15 = 32767u;
    if (next.feedback_q15 > 24576u) next.feedback_q15 = 24576u;

    if (next.enabled && !was_enabled) {
        /* Fresh engage: drop any stale tail and start the gains at their
         * targets — the buffer is silent, so there is nothing to de-click. */
        audio_delay_fx_reset(fx);
        fx->wet_cur_q15 = next.wet_q15;
        fx->feedback_cur_q15 = next.feedback_q15;
    } else if (!next.enabled && was_enabled && fx->allocated) {
        /* Switch-off: let the buffered repeats ring out instead of cutting. */
        fx->tail_frames_remaining = fx->sample_rate * AUDIO_DELAY_FX_TAIL_SECONDS;
    }

    fx->config = next;

    uint64_t frames = ((uint64_t)fx->sample_rate * (uint64_t)fx->config.delay_ms + 999u) / 1000u;
    if (frames < 1u) frames = 1u;
    if (fx->capacity_frames > 0u && frames >= fx->capacity_frames) {
        frames = fx->capacity_frames - 1u;
    }
    fx->delay_frames = (uint32_t)frames;

    uint32_t fs = fx->sample_rate ? fx->sample_rate : 44100u;
    fx->damp_alpha_q15 = (uint16_t)((32768u * AUDIO_DELAY_FX_DAMP_OMEGA) /
                                    (AUDIO_DELAY_FX_DAMP_OMEGA + fs));
}

static int32_t damp_feedback_sample(audio_delay_fx_t *fx, int16_t delayed, uint8_t channel)
{
    fx->fb_lp[channel] += (((int32_t)delayed - fx->fb_lp[channel]) *
                           (int32_t)fx->damp_alpha_q15) >> 15;
    return fx->fb_lp[channel];
}

audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in)
{
    if (!fx || !fx->allocated || fx->delay_frames == 0u) {
        return in;
    }
    bool active = fx->config.enabled;
    bool ringing = fx->tail_frames_remaining > 0u;
    if (!active && !ringing) {
        return in;
    }

    fx->wet_cur_q15 = smooth_q15(fx->wet_cur_q15, fx->config.wet_q15);
    fx->feedback_cur_q15 = smooth_q15(fx->feedback_cur_q15, fx->config.feedback_q15);

    uint32_t read_index = fx->write_index >= fx->delay_frames
        ? fx->write_index - fx->delay_frames
        : fx->write_index + fx->capacity_frames - fx->delay_frames;
    int16_t delayed_l = fx->left[read_index];
    int16_t delayed_r = fx->right[read_index];

    int32_t out_l = (int32_t)in.left + q15_mul_i32(delayed_l, fx->wet_cur_q15);
    int32_t out_r = (int32_t)in.right + q15_mul_i32(delayed_r, fx->wet_cur_q15);

    int32_t fb_l = q15_mul_i32(damp_feedback_sample(fx, delayed_l, 0), fx->feedback_cur_q15);
    int32_t fb_r = q15_mul_i32(damp_feedback_sample(fx, delayed_r, 1), fx->feedback_cur_q15);
    /* While ringing out, the input no longer feeds the line — only the
     * damped feedback keeps circulating until the tail window closes. */
    int32_t write_l = active ? (int32_t)in.left + fb_l : fb_l;
    int32_t write_r = active ? (int32_t)in.right + fb_r : fb_r;
    fx->left[fx->write_index] = clamp_i16(write_l);
    fx->right[fx->write_index] = clamp_i16(write_r);
    fx->write_index++;
    if (fx->write_index >= fx->capacity_frames) {
        fx->write_index = 0u;
    }

    if (!active && ringing) {
        fx->tail_frames_remaining--;
    }

    return (audio_mixer_frame_t) {
        .left = clamp_i16(out_l),
        .right = clamp_i16(out_r),
    };
}

bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx)
{
    return fx && fx->allocated;
}

bool audio_delay_fx_is_ringing(const audio_delay_fx_t *fx)
{
    return fx && fx->tail_frames_remaining > 0u;
}

uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx)
{
    return fx ? fx->config.delay_ms : 0u;
}
