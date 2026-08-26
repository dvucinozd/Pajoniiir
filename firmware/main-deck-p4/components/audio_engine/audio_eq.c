#include "audio_eq.h"

#include <stddef.h>

#define AUDIO_EQ_LOW_CUTOFF_HZ  800.0f
#define AUDIO_EQ_HIGH_CUTOFF_HZ 4000.0f
#define AUDIO_EQ_PI             3.14159265358979323846f
#define AUDIO_EQ_GAIN_BLOCK     32u

static uint16_t clamp_raw(uint16_t raw)
{
    return raw > AUDIO_EQ_RAW_MAX ? AUDIO_EQ_RAW_MAX : raw;
}

static float one_pole_alpha(float cutoff_hz, uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0u) {
        sample_rate_hz = 44100u;
    }
    const float omega = 2.0f * AUDIO_EQ_PI * cutoff_hz;
    return omega / (omega + (float)sample_rate_hz);
}

float audio_eq_raw_to_gain(uint16_t raw)
{
    raw = clamp_raw(raw);
    if (raw <= AUDIO_EQ_RAW_CENTER) {
        return (float)raw / (float)AUDIO_EQ_RAW_CENTER;
    }
    return 1.0f + ((float)(raw - AUDIO_EQ_RAW_CENTER) /
                   (float)(AUDIO_EQ_RAW_MAX - AUDIO_EQ_RAW_CENTER));
}

void audio_eq_reset_filters(audio_eq_state_t *eq)
{
    if (!eq) return;
    eq->low_lp[0] = 0.0f;
    eq->low_lp[1] = 0.0f;
    eq->low_lp2[0] = 0.0f;
    eq->low_lp2[1] = 0.0f;
    eq->high_lp[0] = 0.0f;
    eq->high_lp[1] = 0.0f;
    eq->high_lp2[0] = 0.0f;
    eq->high_lp2[1] = 0.0f;
}

void audio_eq_init(audio_eq_state_t *eq, uint32_t sample_rate_hz)
{
    if (!eq) return;
    audio_eq_set_sample_rate(eq, sample_rate_hz);
    for (uint8_t band = 0u; band < AUDIO_EQ_BAND_COUNT; band++) {
        __atomic_store_n(&eq->raw[band], AUDIO_EQ_RAW_CENTER, __ATOMIC_RELAXED);
        eq->applied_raw[band] = AUDIO_EQ_RAW_CENTER;
        eq->gain[band] = 1.0f;
    }
    eq->gain_frames_left = 0u;
    audio_eq_reset_filters(eq);
}

/* Retunes the band-split alphas without touching gains or filter state, so it
 * is safe once the real output rate is known mid-stream. */
void audio_eq_set_sample_rate(audio_eq_state_t *eq, uint32_t sample_rate_hz)
{
    if (!eq) return;
    eq->low_alpha = one_pole_alpha(AUDIO_EQ_LOW_CUTOFF_HZ, sample_rate_hz);
    eq->high_alpha = one_pole_alpha(AUDIO_EQ_HIGH_CUTOFF_HZ, sample_rate_hz);
}

void audio_eq_set_raw(audio_eq_state_t *eq,
                      uint16_t low,
                      uint16_t mid,
                      uint16_t high)
{
    if (!eq) return;
    audio_eq_set_band_raw(eq, AUDIO_EQ_BAND_LOW, low);
    audio_eq_set_band_raw(eq, AUDIO_EQ_BAND_MID, mid);
    audio_eq_set_band_raw(eq, AUDIO_EQ_BAND_HIGH, high);
}

void audio_eq_set_band_raw(audio_eq_state_t *eq, audio_eq_band_t band, uint16_t raw)
{
    if (!eq || band >= AUDIO_EQ_BAND_COUNT) return;
    raw = clamp_raw(raw);
    __atomic_store_n(&eq->raw[band], raw, __ATOMIC_RELEASE);
}

uint16_t audio_eq_get_band_raw(const audio_eq_state_t *eq, audio_eq_band_t band)
{
    if (!eq || band >= AUDIO_EQ_BAND_COUNT) return AUDIO_EQ_RAW_CENTER;
    return __atomic_load_n(&eq->raw[band], __ATOMIC_ACQUIRE);
}

static void refresh_gains(audio_eq_state_t *eq)
{
    for (uint8_t band = 0u; band < AUDIO_EQ_BAND_COUNT; band++) {
        uint16_t raw = __atomic_load_n(&eq->raw[band], __ATOMIC_ACQUIRE);
        if (raw != eq->applied_raw[band]) {
            eq->applied_raw[band] = raw;
            eq->gain[band] = audio_eq_raw_to_gain(raw);
        }
    }
}

static float process_sample(audio_eq_state_t *eq, float sample, uint8_t channel)
{
    eq->low_lp[channel] += eq->low_alpha * (sample - eq->low_lp[channel]);
    eq->low_lp2[channel] += eq->low_alpha * (eq->low_lp[channel] - eq->low_lp2[channel]);
    eq->high_lp[channel] += eq->high_alpha * (sample - eq->high_lp[channel]);
    eq->high_lp2[channel] += eq->high_alpha * (eq->high_lp[channel] - eq->high_lp2[channel]);

    const float low = eq->low_lp2[channel];
    const float high = sample - eq->high_lp2[channel];
    const float mid = sample - low - high;

    return (low * eq->gain[AUDIO_EQ_BAND_LOW]) +
           (mid * eq->gain[AUDIO_EQ_BAND_MID]) +
           (high * eq->gain[AUDIO_EQ_BAND_HIGH]);
}

audio_dsp_frame_t audio_eq_process_dsp_frame(audio_eq_state_t *eq,
                                             audio_dsp_frame_t in)
{
    if (!eq) return in;
    if (eq->gain_frames_left == 0u) {
        refresh_gains(eq);
        eq->gain_frames_left = AUDIO_EQ_GAIN_BLOCK;
    }
    eq->gain_frames_left--;
    return (audio_dsp_frame_t) {
        .left = process_sample(eq, in.left, 0),
        .right = process_sample(eq, in.right, 1),
    };
}

audio_mixer_frame_t audio_eq_process_frame(audio_eq_state_t *eq,
                                           audio_mixer_frame_t in)
{
    return audio_mixer_pcm_from_dsp(audio_eq_process_dsp_frame(
        eq, audio_mixer_dsp_from_pcm(in, 1.0f)));
}
