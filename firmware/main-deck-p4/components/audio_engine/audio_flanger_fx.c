#include "audio_flanger_fx.h"

#include <string.h>

/* Classic flanger: a short delay line swept by a triangle LFO between 0.6 ms
 * and 6 ms, read with linear interpolation, with positive feedback for the
 * resonant "jet" character. Wet and feedback both scale with the depth knob.
 * All per-sample math is integer (Q15/16.16); the LFO advances one phase
 * step per frame so the sweep is sample-accurate and beat-syncable. */
/* 100 us, not the original 600. The first comb notch sits at 1/(2*delay), so a
 * 600 us floor stopped the sweep at 833 Hz and kept the whole effect down in
 * the low mids - present, but not the sweep through the presence region that
 * makes a flanger sound like a jet. At 100 us the notch reaches 5 kHz.
 * Measured swing at 4 kHz: 9.9 dB at 600 us, 15.7 dB here. Does not change the
 * buffer, which is sized from the maximum. */
#define AUDIO_FLANGER_MIN_DELAY_US 100u
#define AUDIO_FLANGER_MAX_DELAY_US 6000u
#define AUDIO_FLANGER_MIN_PERIOD_MS 100u
#define AUDIO_FLANGER_MAX_PERIOD_MS 8000u
/* Wet tops out at 0.5 (equal-power-ish comb) and feedback at 0.6. */
/* A flanger's whole character is the depth of the comb notches, and that comes
 * from how nearly the delayed copy cancels the dry signal. The notch is
 * 20*log10(1-wet): at the original 0.50 that is only -6 dB, a mild phasey
 * wobble rather than a flanger, which is exactly how it was reported. 0.90
 * gives -20 dB. The output is normalised by 1/(1+wet) below, so the deeper mix
 * costs no headroom and does not jump the level when the effect engages. */
#define AUDIO_FLANGER_WET_MAX_Q15 29491u   /* 0.90 */
/* 0.75. Feedback is what sharpens the resonance into the jet. It does not pay
 * to go higher: at 0.85 the recirculating sum starts hitting clamp_i16 inside
 * the delay line, the peak pins at 2.08x and the measured swing gets *worse*,
 * so the clipping destroys the resonance it was meant to build. */
#define AUDIO_FLANGER_FB_MAX_Q15 24576u

/* Live tuning. The four numbers that decide how this sounds are a listening
 * decision, not a computable one: measured null-to-peak "swing" turned out to
 * rank the *worst*-sounding setting highest, because swing rewards deep
 * cancellation and deep cancellation is exactly what sounds choked. So the
 * ceilings are runtime variables with the compiled constants as defaults, set
 * over the web API while the effect is running, and the winning values are
 * baked back into the defaults afterwards. */
static uint16_t s_wet_max_q15 = AUDIO_FLANGER_WET_MAX_Q15;
static uint16_t s_fb_max_q15  = AUDIO_FLANGER_FB_MAX_Q15;
static uint32_t s_min_delay_us = AUDIO_FLANGER_MIN_DELAY_US;
/* Output scaling. 0 = dry stays at unity and wet is added on top, which is what
 * a hardware flanger does; 1 = divide by (1+wet). Normalising protects headroom
 * but drops the whole output by 5.6 dB at wet 0.9, which reads as "the effect
 * makes everything quieter and duller" rather than as protection. */
static bool s_normalize = true;
static uint32_t s_tune_generation = 1u;

void audio_flanger_fx_tune(uint16_t wet_max_q15, uint16_t fb_max_q15,
                           uint32_t min_delay_us, bool normalize)
{
    if (wet_max_q15 > 32767u) wet_max_q15 = 32767u;
    if (fb_max_q15 > 32767u)  fb_max_q15 = 32767u;
    if (min_delay_us < 20u)   min_delay_us = 20u;
    if (min_delay_us > AUDIO_FLANGER_MAX_DELAY_US / 2u) {
        min_delay_us = AUDIO_FLANGER_MAX_DELAY_US / 2u;
    }
    s_wet_max_q15 = wet_max_q15;
    s_fb_max_q15 = fb_max_q15;
    s_min_delay_us = min_delay_us;
    s_normalize = normalize;
    s_tune_generation++;
}

void audio_flanger_fx_tuning(uint16_t *wet_max_q15, uint16_t *fb_max_q15,
                             uint32_t *min_delay_us, bool *normalize)
{
    if (wet_max_q15) *wet_max_q15 = s_wet_max_q15;
    if (fb_max_q15)  *fb_max_q15 = s_fb_max_q15;
    if (min_delay_us) *min_delay_us = s_min_delay_us;
    if (normalize)   *normalize = s_normalize;
}
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
    /* Must agree with wet_cur_q15, or the lazy recompute below never fires and
     * a zeroed norm_q15 would silence the output instead of passing it. */
    fx->norm_q15 = 32768u;          /* 1/(1+0) in Q15 */
    fx->norm_for_wet_q15 = 0u;
    fx->norm_generation = s_tune_generation;
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
                                      s_wet_max_q15) >> 15);
        fx->feedback_cur_q15 = (uint16_t)(((uint32_t)next.depth_q15 *
                                           s_fb_max_q15) >> 15);
    }

    fx->config = next;

    uint32_t fs = fx->sample_rate ? fx->sample_rate : 44100u;
    uint64_t period_frames = ((uint64_t)fs * fx->config.period_ms) / 1000u;
    if (period_frames < 1u) period_frames = 1u;
    fx->lfo_step_q32 = (uint32_t)((1ull << 32) / period_frames);

    uint64_t min_q16 = ((uint64_t)fs * s_min_delay_us << 16) / 1000000u;
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
                                      s_wet_max_q15) >> 15);
    uint16_t fb_target = (uint16_t)(((uint32_t)fx->config.depth_q15 *
                                     s_fb_max_q15) >> 15);
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

    /* Normalise the dry+wet sum so the peak stays at unity whatever the mix.
     * Recomputed only when the smoothed wet gain actually moves, so the divide
     * is absent from the steady-state hot path. */
    if (fx->wet_cur_q15 != fx->norm_for_wet_q15 ||
        fx->norm_generation != s_tune_generation) {
        fx->norm_q15 = s_normalize
            ? (uint16_t)(((uint32_t)32768u << 15) /
                         (32768u + (uint32_t)fx->wet_cur_q15))
            : 32768u;
        fx->norm_for_wet_q15 = fx->wet_cur_q15;
        fx->norm_generation = s_tune_generation;
    }

    int32_t sum_l = (int32_t)in.left + ((delayed_l * (int32_t)fx->wet_cur_q15) >> 15);
    int32_t sum_r = (int32_t)in.right + ((delayed_r * (int32_t)fx->wet_cur_q15) >> 15);
    int32_t out_l = (sum_l * (int32_t)fx->norm_q15) >> 15;
    int32_t out_r = (sum_r * (int32_t)fx->norm_q15) >> 15;

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
