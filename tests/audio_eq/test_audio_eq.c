#include "audio_eq.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_SAMPLE_RATE 44100u
#define TEST_FRAMES 44100u

static float rms_of_sine_after_eq(float freq_hz,
                                  uint16_t low,
                                  uint16_t mid,
                                  uint16_t high)
{
    audio_eq_state_t eq;
    audio_eq_init(&eq, TEST_SAMPLE_RATE);
    audio_eq_set_raw(&eq, low, mid, high);

    double sum_sq = 0.0;
    for (uint32_t i = 0; i < TEST_FRAMES; ++i) {
        float phase = 2.0f * 3.14159265358979323846f * freq_hz * (float)i / (float)TEST_SAMPLE_RATE;
        int16_t sample = (int16_t)(sinf(phase) * 12000.0f);
        audio_mixer_frame_t out = audio_eq_process_frame(&eq, (audio_mixer_frame_t) {
            .left = sample,
            .right = sample,
        });
        float normalized = (float)out.left / 32768.0f;
        sum_sq += (double)normalized * (double)normalized;
    }
    return sqrtf((float)(sum_sq / (double)TEST_FRAMES));
}

static void test_center_eq_keeps_signal_level_near_unity(void)
{
    float dry = rms_of_sine_after_eq(1000.0f,
                                     AUDIO_EQ_RAW_CENTER,
                                     AUDIO_EQ_RAW_CENTER,
                                     AUDIO_EQ_RAW_CENTER);
    float boosted_center = rms_of_sine_after_eq(1000.0f,
                                                AUDIO_EQ_RAW_CENTER,
                                                AUDIO_EQ_RAW_CENTER,
                                                AUDIO_EQ_RAW_CENTER);
    assert(boosted_center > dry * 0.98f);
    assert(boosted_center < dry * 1.02f);
}

static void test_low_kill_reduces_bass_more_than_treble(void)
{
    float low_killed_bass = rms_of_sine_after_eq(100.0f,
                                                 AUDIO_EQ_RAW_MIN,
                                                 AUDIO_EQ_RAW_CENTER,
                                                 AUDIO_EQ_RAW_CENTER);
    float normal_bass = rms_of_sine_after_eq(100.0f,
                                             AUDIO_EQ_RAW_CENTER,
                                             AUDIO_EQ_RAW_CENTER,
                                             AUDIO_EQ_RAW_CENTER);
    float low_killed_treble = rms_of_sine_after_eq(8000.0f,
                                                   AUDIO_EQ_RAW_MIN,
                                                   AUDIO_EQ_RAW_CENTER,
                                                   AUDIO_EQ_RAW_CENTER);
    float normal_treble = rms_of_sine_after_eq(8000.0f,
                                               AUDIO_EQ_RAW_CENTER,
                                               AUDIO_EQ_RAW_CENTER,
                                               AUDIO_EQ_RAW_CENTER);

    assert(low_killed_bass < normal_bass * 0.35f);
    assert(low_killed_treble > normal_treble * 0.80f);
}

static void test_high_kill_reduces_treble_more_than_bass(void)
{
    float high_killed_treble = rms_of_sine_after_eq(8000.0f,
                                                    AUDIO_EQ_RAW_CENTER,
                                                    AUDIO_EQ_RAW_CENTER,
                                                    AUDIO_EQ_RAW_MIN);
    float normal_treble = rms_of_sine_after_eq(8000.0f,
                                               AUDIO_EQ_RAW_CENTER,
                                               AUDIO_EQ_RAW_CENTER,
                                               AUDIO_EQ_RAW_CENTER);
    float high_killed_bass = rms_of_sine_after_eq(100.0f,
                                                  AUDIO_EQ_RAW_CENTER,
                                                  AUDIO_EQ_RAW_CENTER,
                                                  AUDIO_EQ_RAW_MIN);
    float normal_bass = rms_of_sine_after_eq(100.0f,
                                             AUDIO_EQ_RAW_CENTER,
                                             AUDIO_EQ_RAW_CENTER,
                                             AUDIO_EQ_RAW_CENTER);

    assert(high_killed_treble < normal_treble * 0.35f);
    assert(high_killed_bass > normal_bass * 0.80f);
}

static void test_max_boost_increases_band_without_wrapping(void)
{
    float normal_mid = rms_of_sine_after_eq(1000.0f,
                                            AUDIO_EQ_RAW_CENTER,
                                            AUDIO_EQ_RAW_CENTER,
                                            AUDIO_EQ_RAW_CENTER);
    float boosted_mid = rms_of_sine_after_eq(1000.0f,
                                             AUDIO_EQ_RAW_CENTER,
                                             AUDIO_EQ_RAW_MAX,
                                             AUDIO_EQ_RAW_CENTER);

    assert(boosted_mid > normal_mid * 1.35f);

    audio_eq_state_t eq;
    audio_eq_init(&eq, TEST_SAMPLE_RATE);
    audio_eq_set_raw(&eq, AUDIO_EQ_RAW_MAX, AUDIO_EQ_RAW_MAX, AUDIO_EQ_RAW_MAX);
    audio_mixer_frame_t out = audio_eq_process_frame(&eq, (audio_mixer_frame_t) {
        .left = 30000,
        .right = -30000,
    });
    assert(out.left <= 32767);
    assert(out.right >= -32768);
}

static void assert_band_boost_preserves_wide_headroom(float freq_hz,
                                                       audio_eq_band_t band)
{
    audio_eq_state_t eq;
    audio_eq_init(&eq, TEST_SAMPLE_RATE);
    audio_eq_set_band_raw(&eq, band, AUDIO_EQ_RAW_MAX);

    float peak = 0.0f;
    for (uint32_t i = 0; i < TEST_FRAMES; ++i) {
        float phase = 2.0f * 3.14159265358979323846f * freq_hz *
                      (float)i / (float)TEST_SAMPLE_RATE;
        float sample = sinf(phase) * 25000.0f;
        audio_dsp_frame_t out = audio_eq_process_dsp_frame(&eq,
            (audio_dsp_frame_t) { .left = sample, .right = sample });
        assert(isfinite(out.left));
        float magnitude = fabsf(out.left);
        if (magnitude > peak) peak = magnitude;
    }

    /* Each boosted band can legitimately exceed PCM full scale. The wide
     * path must carry that headroom to the final output limiter. */
    assert(peak > 32768.0f);
    assert(peak < 60000.0f);
}

static void test_each_band_boost_preserves_wide_headroom(void)
{
    assert_band_boost_preserves_wide_headroom(100.0f, AUDIO_EQ_BAND_LOW);
    assert_band_boost_preserves_wide_headroom(1000.0f, AUDIO_EQ_BAND_MID);
    assert_band_boost_preserves_wide_headroom(8000.0f, AUDIO_EQ_BAND_HIGH);
}

int main(void)
{
    test_center_eq_keeps_signal_level_near_unity();
    test_low_kill_reduces_bass_more_than_treble();
    test_high_kill_reduces_treble_more_than_bass();
    test_max_boost_increases_band_without_wrapping();
    test_each_band_boost_preserves_wide_headroom();
    puts("audio_eq tests passed");
    return 0;
}
