#include "audio_filter.h"

#include <math.h>

/* DJ channel filter: one knob, low-pass left of centre, high-pass right of
 * centre. Implemented as a ZDF (topology-preserving transform) state-variable
 * filter so the sweep stays stable at any cutoff, with a mild resonant bump
 * for the classic DJ-filter character. The cutoff moves exponentially with
 * the knob so every degree of turn is worth the same musical interval. */
#define AUDIO_FILTER_PI             3.14159265358979323846f
#define AUDIO_FILTER_LP_MAX_HZ      18000.0f
#define AUDIO_FILTER_LP_MIN_HZ      60.0f
#define AUDIO_FILTER_HP_MIN_HZ      20.0f
#define AUDIO_FILTER_HP_MAX_HZ      8000.0f
/* k = 1/Q; 0.8 gives Q = 1.25 (~ +2 dB bump at the cutoff). */
#define AUDIO_FILTER_RES_K          0.8f
#define AUDIO_FILTER_CENTER_DEAD_RAW 96u
/* Coefficients (and the smoothed knob position) refresh once per block. */
#define AUDIO_FILTER_SMOOTH_BLOCK   32u
#define AUDIO_FILTER_SMOOTH_COEF    0.2f
#define AUDIO_FILTER_SMOOTH_SNAP_RAW 0.5f
/* Precomputed logarithms remove two invariant libm calls from every
 * coefficient refresh. */
#define AUDIO_FILTER_LP_LOG_RATIO   (-5.7037824747f) /* log(60 / 18000) */
#define AUDIO_FILTER_HP_LOG_RATIO   5.9914645471f    /* log(8000 / 20) */

static int16_t clamp_i16_from_float(float sample)
{
    if (sample > 32767.0f) return 32767;
    if (sample < -32768.0f) return -32768;
    return (int16_t)(sample >= 0.0f ? sample + 0.5f : sample - 0.5f);
}

void audio_filter_reset(audio_filter_state_t *filter)
{
    if (!filter) return;
    filter->ic1eq[0] = 0.0f;
    filter->ic1eq[1] = 0.0f;
    filter->ic2eq[0] = 0.0f;
    filter->ic2eq[1] = 0.0f;
    filter->smoothed_raw = (float)__atomic_load_n(&filter->raw, __ATOMIC_ACQUIRE);
    filter->block_frames_left = 0;
    filter->coefficients_dirty = true;
    filter->bypassed = true;
}

void audio_filter_init(audio_filter_state_t *filter, uint32_t sample_rate_hz)
{
    if (!filter) return;
    __atomic_store_n(&filter->raw, AUDIO_FILTER_RAW_CENTER, __ATOMIC_RELAXED);
    filter->sample_rate_hz = 44100u;
    filter->hp_mode = false;
    filter->k = AUDIO_FILTER_RES_K;
    filter->a1 = 0.0f;
    filter->a2 = 0.0f;
    filter->a3 = 0.0f;
    audio_filter_set_sample_rate(filter, sample_rate_hz);
    audio_filter_reset(filter);
}

void audio_filter_set_sample_rate(audio_filter_state_t *filter, uint32_t sample_rate_hz)
{
    if (!filter) return;
    uint32_t next = sample_rate_hz ? sample_rate_hz : 44100u;
    if (next != filter->sample_rate_hz) {
        filter->sample_rate_hz = next;
        filter->coefficients_dirty = true;
        filter->block_frames_left = 0u;
    }
}

void audio_filter_set_raw(audio_filter_state_t *filter, uint16_t raw)
{
    if (!filter) return;
    if (raw > AUDIO_FILTER_RAW_MAX) {
        raw = AUDIO_FILTER_RAW_MAX;
    }
    __atomic_store_n(&filter->raw, raw, __ATOMIC_RELEASE);
}

uint16_t audio_filter_get_raw(const audio_filter_state_t *filter)
{
    return filter ? __atomic_load_n(&filter->raw, __ATOMIC_ACQUIRE)
                  : AUDIO_FILTER_RAW_CENTER;
}

static void audio_filter_update_coefficients(audio_filter_state_t *filter)
{
    uint16_t target_raw = __atomic_load_n(&filter->raw, __ATOMIC_ACQUIRE);
    float target = (float)target_raw;
    float movement = target - filter->smoothed_raw;
    bool position_changed = false;
    if (fabsf(movement) <= AUDIO_FILTER_SMOOTH_SNAP_RAW) {
        if (filter->smoothed_raw != target) {
            filter->smoothed_raw = target;
            position_changed = true;
        }
    } else {
        filter->smoothed_raw += movement * AUDIO_FILTER_SMOOTH_COEF;
        position_changed = true;
    }

    float delta = filter->smoothed_raw - (float)AUDIO_FILTER_RAW_CENTER;
    float mag = fabsf(delta);
    uint16_t raw_dist = target_raw > AUDIO_FILTER_RAW_CENTER
        ? target_raw - AUDIO_FILTER_RAW_CENTER
        : AUDIO_FILTER_RAW_CENTER - target_raw;
    if (raw_dist <= AUDIO_FILTER_CENTER_DEAD_RAW &&
        mag <= (float)AUDIO_FILTER_CENTER_DEAD_RAW) {
        filter->bypassed = true;
        return;
    }
    bool was_bypassed = filter->bypassed;
    bool next_hp_mode = delta >= 0.0f;
    filter->bypassed = false;
    if (!position_changed && !filter->coefficients_dirty &&
        !was_bypassed && filter->hp_mode == next_hp_mode) {
        return;
    }

    float intensity = mag / (float)AUDIO_FILTER_RAW_CENTER;
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }

    float cutoff;
    if (delta < 0.0f) {
        filter->hp_mode = false;
        /* 18 kHz at the detent down to 60 Hz at full kill. */
        cutoff = AUDIO_FILTER_LP_MAX_HZ *
                 expf(AUDIO_FILTER_LP_LOG_RATIO * intensity);
    } else {
        filter->hp_mode = true;
        /* 20 Hz at the detent up to 8 kHz at full kill. */
        cutoff = AUDIO_FILTER_HP_MIN_HZ *
                 expf(AUDIO_FILTER_HP_LOG_RATIO * intensity);
    }

    float fs = filter->sample_rate_hz ? (float)filter->sample_rate_hz : 44100.0f;
    float max_cutoff = 0.45f * fs;
    if (cutoff > max_cutoff) {
        cutoff = max_cutoff;
    }

    float g = tanf(AUDIO_FILTER_PI * cutoff / fs);
    filter->k = AUDIO_FILTER_RES_K;
    filter->a1 = 1.0f / (1.0f + g * (g + filter->k));
    filter->a2 = g * filter->a1;
    filter->a3 = g * filter->a2;
    filter->coefficients_dirty = false;
}

static float svf_process(audio_filter_state_t *filter, float sample, uint8_t channel)
{
    float v3 = sample - filter->ic2eq[channel];
    float v1 = filter->a1 * filter->ic1eq[channel] + filter->a2 * v3;
    float v2 = filter->ic2eq[channel] + filter->a2 * filter->ic1eq[channel] +
               filter->a3 * v3;
    filter->ic1eq[channel] = 2.0f * v1 - filter->ic1eq[channel];
    filter->ic2eq[channel] = 2.0f * v2 - filter->ic2eq[channel];
    if (filter->hp_mode) {
        return sample - filter->k * v1 - v2;
    }
    return v2;
}

audio_mixer_frame_t audio_filter_process_frame(audio_filter_state_t *filter,
                                               bool enabled,
                                               audio_mixer_frame_t in)
{
    if (!filter || !enabled) {
        return in;
    }

    if (filter->block_frames_left == 0u) {
        filter->block_frames_left = AUDIO_FILTER_SMOOTH_BLOCK;
        audio_filter_update_coefficients(filter);
    }
    filter->block_frames_left--;

    if (filter->bypassed) {
        return in;
    }

    return (audio_mixer_frame_t) {
        .left = clamp_i16_from_float(svf_process(filter, (float)in.left, 0)),
        .right = clamp_i16_from_float(svf_process(filter, (float)in.right, 1)),
    };
}
