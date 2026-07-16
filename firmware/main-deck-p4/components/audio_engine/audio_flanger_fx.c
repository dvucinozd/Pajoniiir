#include "audio_flanger_fx.h"

#include <string.h>

/* Classic flanger: a short delay line swept by a triangle LFO between 0.6 ms
 * and 6 ms, read with linear interpolation, with positive feedback for the
 * resonant "jet" character. Wet and feedback both scale with the depth knob.
 * All per-sample math is integer (Q15/16.16); the LFO advances one phase
 * step per frame so the sweep is sample-accurate and beat-syncable. */
#define AUDIO_FLANGER_MIN_DELAY_US 600u
#define AUDIO_FLANGER_MAX_DELAY_US 6000u
#define AUDIO_FLANGER_MIN_PERIOD_MS 100u
#define AUDIO_FLANGER_MAX_PERIOD_MS 8000u
/* Wet tops out at 0.5 (equal-power-ish comb) and feedback at 0.6. */
#define AUDIO_FLANGER_WET_MAX_Q15 16384u
#define AUDIO_FLANGER_FB_MAX_Q15 19660u
#define AUDIO_FLANGER_SMOOTH_SHIFT 6

static int16_t clamp_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static uint16_t smooth_q15(uint16_t current, uint16_t target)
{
    int32_t delta = (int32_t)target - (int32_t)current;
    int32_t step = delta / (1 << AUDIO_FLANGER_SMOOTH_SHIFT);
    if (step == 0 && delta != 0) {
        step = delta > 0 ? 1 : -1;
    }
    return (uint16_t)((int32_t)current + step);
}

uint32_t audio_flanger_fx_required_frames(uint32_t sample_rate)
{
    uint64_t max_frames = ((uint64_t)sample_rate * AUDIO_FLANGER_MAX_DELAY_US +
                           999999u) / 1000000u;
    return (uint32_t)max_frames + 4u;
}

void audio_flanger_fx_init(audio_flanger_fx_t *fx,
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
    fx->allocated = left && right && sample_rate > 0u &&
                    capacity_frames >= audio_flanger_fx_required_frames(sample_rate);
    audio_flanger_fx_reset(fx);
}

void audio_flanger_fx_reset(audio_flanger_fx_t *fx)
{
    if (!fx) return;
    fx->write_index = 0;
    fx->lfo_phase_q32 = 0;
    fx->wet_cur_q15 = 0;
    fx->feedback_cur_q15 = 0;
    if (fx->left && fx->capacity_frames > 0u) {
        memset(fx->left, 0, fx->capacity_frames * sizeof(fx->left[0]));
    }
    if (fx->right && fx->capacity_frames > 0u) {
        memset(fx->right, 0, fx->capacity_frames * sizeof(fx->right[0]));
    }
}

void audio_flanger_fx_configure(audio_flanger_fx_t *fx,
                                const audio_flanger_fx_config_t *config)
{
    if (!fx || !config) return;
    bool was_enabled = fx->config.enabled;
    audio_flanger_fx_config_t next = *config;
    if (next.depth_q15 > 32767u) next.depth_q15 = 32767u;
    if (next.period_ms < AUDIO_FLANGER_MIN_PERIOD_MS) {
        next.period_ms = AUDIO_FLANGER_MIN_PERIOD_MS;
    }
    if (next.period_ms > AUDIO_FLANGER_MAX_PERIOD_MS) {
        next.period_ms = AUDIO_FLANGER_MAX_PERIOD_MS;
    }

    if (next.enabled && !was_enabled) {
        /* Fresh engage on a silent line: start gains at target, no de-click
         * ramp needed. */
        audio_flanger_fx_reset(fx);
        fx->wet_cur_q15 = (uint16_t)(((uint32_t)next.depth_q15 *
                                      AUDIO_FLANGER_WET_MAX_Q15) >> 15);
        fx->feedback_cur_q15 = (uint16_t)(((uint32_t)next.depth_q15 *
                                           AUDIO_FLANGER_FB_MAX_Q15) >> 15);
    }

    fx->config = next;

    uint32_t fs = fx->sample_rate ? fx->sample_rate : 44100u;
    uint64_t period_frames = ((uint64_t)fs * fx->config.period_ms) / 1000u;
    if (period_frames < 1u) period_frames = 1u;
    fx->lfo_step_q32 = (uint32_t)((1ull << 32) / period_frames);

    uint64_t min_q16 = ((uint64_t)fs * AUDIO_FLANGER_MIN_DELAY_US << 16) / 1000000u;
    uint64_t max_q16 = ((uint64_t)fs * AUDIO_FLANGER_MAX_DELAY_US << 16) / 1000000u;
    fx->min_delay_q16 = (uint32_t)min_q16;
    fx->span_delay_q16 = (uint32_t)(max_q16 - min_q16);
}

static int32_t read_delayed(const int16_t *buffer,
                            uint32_t idx0,
                            uint32_t idx1,
                            uint32_t frac_q16)
{
    int32_t s0 = buffer[idx0];
    int32_t s1 = buffer[idx1];
    /* A full-scale -32768 -> +32767 step multiplied by 0xffff exceeds
     * signed int32.  Widen before the multiply so hostile or clipped input
     * cannot invoke undefined behaviour in the audio task. */
    return s0 + (int32_t)(((int64_t)(s1 - s0) * (int64_t)frac_q16) >> 16);
}

audio_mixer_frame_t audio_flanger_fx_process_frame(audio_flanger_fx_t *fx,
                                                   audio_mixer_frame_t in)
{
    if (!fx || !fx->allocated || !fx->config.enabled) {
        return in;
    }

    uint16_t wet_target = (uint16_t)(((uint32_t)fx->config.depth_q15 *
                                      AUDIO_FLANGER_WET_MAX_Q15) >> 15);
    uint16_t fb_target = (uint16_t)(((uint32_t)fx->config.depth_q15 *
                                     AUDIO_FLANGER_FB_MAX_Q15) >> 15);
    fx->wet_cur_q15 = smooth_q15(fx->wet_cur_q15, wet_target);
    fx->feedback_cur_q15 = smooth_q15(fx->feedback_cur_q15, fb_target);

    fx->lfo_phase_q32 += fx->lfo_step_q32;
    uint32_t phase = fx->lfo_phase_q32;
    /* Triangle in 0..65536 (Q16). */
    uint32_t tri_q16 = phase < 0x80000000u ? (phase >> 15)
                                           : ((0xFFFFFFFFu - phase) >> 15);
    uint32_t delay_q16 = fx->min_delay_q16 +
                         (uint32_t)(((uint64_t)fx->span_delay_q16 * tri_q16) >> 16);
    uint32_t delay_int = delay_q16 >> 16;
    uint32_t frac_q16 = delay_q16 & 0xFFFFu;

    uint32_t idx0 = fx->write_index >= delay_int
        ? fx->write_index - delay_int
        : fx->write_index + fx->capacity_frames - delay_int;
    uint32_t idx1 = idx0 == 0u ? fx->capacity_frames - 1u : idx0 - 1u;
    int32_t delayed_l = read_delayed(fx->left, idx0, idx1, frac_q16);
    int32_t delayed_r = read_delayed(fx->right, idx0, idx1, frac_q16);

    int32_t out_l = (int32_t)in.left + ((delayed_l * (int32_t)fx->wet_cur_q15) >> 15);
    int32_t out_r = (int32_t)in.right + ((delayed_r * (int32_t)fx->wet_cur_q15) >> 15);

    fx->left[fx->write_index] = clamp_i16(
        (int32_t)in.left + ((delayed_l * (int32_t)fx->feedback_cur_q15) >> 15));
    fx->right[fx->write_index] = clamp_i16(
        (int32_t)in.right + ((delayed_r * (int32_t)fx->feedback_cur_q15) >> 15));
    fx->write_index++;
    if (fx->write_index >= fx->capacity_frames) {
        fx->write_index = 0u;
    }

    return (audio_mixer_frame_t) {
        .left = clamp_i16(out_l),
        .right = clamp_i16(out_r),
    };
}

bool audio_flanger_fx_is_allocated(const audio_flanger_fx_t *fx)
{
    return fx && fx->allocated;
}
