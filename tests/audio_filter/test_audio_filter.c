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

static void test_half_low_pass_keeps_bass_and_kills_treble(void)
{
    uint16_t half_lp = AUDIO_FILTER_RAW_CENTER / 2u;
    float bass = rms_after_filter(100.0f, half_lp, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);
    float treble = rms_after_filter(8000.0f, half_lp, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(bass > normal_bass * 0.85f);
    assert(treble < normal_treble * 0.15f);
}

static void test_full_low_pass_kills_mids_and_most_bass(void)
{
    float mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_MIN, true);
    float normal_mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_MIN, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(mids < normal_mids * 0.10f);
    assert(bass < normal_bass * 0.75f);
}

static void test_low_pass_treble_cut_deepens_monotonically(void)
{
    float normal = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float quarter = rms_after_filter(8000.0f,
                                     AUDIO_FILTER_RAW_CENTER -
                                     (AUDIO_FILTER_RAW_CENTER / 4u), true);
    float half = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER / 2u, true);
    float full = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MIN, true);

    assert(quarter < normal * 0.75f);
    assert(half < quarter * 0.5f);
    assert(full < half * 0.5f);
}

static void test_half_high_pass_kills_bass_and_keeps_treble(void)
{
    uint16_t half_hp = AUDIO_FILTER_RAW_CENTER +
                       ((AUDIO_FILTER_RAW_MAX - AUDIO_FILTER_RAW_CENTER) / 2u);
    float bass = rms_after_filter(100.0f, half_hp, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);
    float treble = rms_after_filter(8000.0f, half_hp, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(bass < normal_bass * 0.20f);
    assert(treble > normal_treble * 0.85f);
}

static void test_full_high_pass_kills_mids_and_keeps_some_treble(void)
{
    float mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_MAX, true);
    float normal_mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MAX, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(mids < normal_mids * 0.15f);
    assert(treble > normal_treble * 0.70f);
}

static void test_resonant_bump_lifts_tone_at_cutoff(void)
{
    /* raw chosen so the low-pass cutoff lands near 8 kHz:
     * intensity = ln(18000/8000) / ln(18000/60) ~= 0.142. */
    uint16_t raw = (uint16_t)(AUDIO_FILTER_RAW_CENTER -
                              (uint16_t)(0.142f * (float)AUDIO_FILTER_RAW_CENTER));
    float at_cutoff = rms_after_filter(8000.0f, raw, true);
    float dry = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    assert(at_cutoff > dry * 1.05f);
    assert(at_cutoff < dry * 1.60f);
}

int main(void)
{
    test_disabled_filter_is_bypass_even_with_extreme_raw();
    test_center_filter_is_bypass_when_enabled();
    test_half_low_pass_keeps_bass_and_kills_treble();
    test_full_low_pass_kills_mids_and_most_bass();
    test_low_pass_treble_cut_deepens_monotonically();
    test_half_high_pass_kills_bass_and_keeps_treble();
    test_full_high_pass_kills_mids_and_keeps_some_treble();
    test_resonant_bump_lifts_tone_at_cutoff();
    puts("audio_filter tests passed");
    return 0;
}
