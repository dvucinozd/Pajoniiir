#include "audio_filter.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_SAMPLE_RATE 44100u
#define TEST_FRAMES 44100u

static float rms_after_filter(float freq_hz, uint16_t raw, bool enabled)
{
    audio_filter_state_t filter;
    audio_filter_init(&filter, TEST_SAMPLE_RATE);
    audio_filter_set_raw(&filter, raw);

    double sum_sq = 0.0;
    for (uint32_t i = 0; i < TEST_FRAMES; ++i) {
        float phase = 2.0f * 3.14159265358979323846f * freq_hz * (float)i / (float)TEST_SAMPLE_RATE;
        int16_t sample = (int16_t)(sinf(phase) * 12000.0f);
        audio_mixer_frame_t out = audio_filter_process_frame(&filter, enabled, (audio_mixer_frame_t) {
            .left = sample,
            .right = sample,
        });
        float normalized = (float)out.left / 32768.0f;
        sum_sq += (double)normalized * (double)normalized;
    }
    return sqrtf((float)(sum_sq / (double)TEST_FRAMES));
}

static void test_disabled_filter_is_bypass_even_with_extreme_raw(void)
{
    float dry = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, false);
    float disabled = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MIN, false);
    assert(disabled > dry * 0.98f);
    assert(disabled < dry * 1.02f);
}

static void test_center_filter_is_bypass_when_enabled(void)
{
    float dry = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, false);
    float center = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, true);
    assert(center > dry * 0.98f);
    assert(center < dry * 1.02f);
}

static void test_left_raw_low_pass_reduces_treble_more_than_bass(void)
{
    float filtered_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MIN, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float filtered_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_MIN, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(filtered_treble < normal_treble * 0.30f);
    assert(filtered_bass > normal_bass * 0.75f);
}

static void test_right_raw_high_pass_reduces_bass_more_than_treble(void)
{
    float filtered_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_MAX, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);
    float filtered_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MAX, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(filtered_bass < normal_bass * 0.30f);
    assert(filtered_treble > normal_treble * 0.75f);
}

int main(void)
{
    test_disabled_filter_is_bypass_even_with_extreme_raw();
    test_center_filter_is_bypass_when_enabled();
    test_left_raw_low_pass_reduces_treble_more_than_bass();
    test_right_raw_high_pass_reduces_bass_more_than_treble();
    puts("audio_filter tests passed");
    return 0;
}
