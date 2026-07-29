#include "audio_filter.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Counted so tests/run_p4_host_tests.ps1 can pin how many assertions this suite
 * executes; a deleted or commented-out test lowers the count and fails the run.
 * Also replaces assert(), which NDEBUG would compile away silently. */
static unsigned s_checks;
#define CHECK(expr) do {                                                     \
    s_checks++;                                                              \
    if (!(expr)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);      \
        abort();                                                             \
    }                                                                        \
} while (0)

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
    CHECK(disabled > dry * 0.98f);
    CHECK(disabled < dry * 1.02f);
}

static void test_center_filter_is_bypass_when_enabled(void)
{
    float dry = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, false);
    float center = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, true);
    CHECK(center > dry * 0.98f);
    CHECK(center < dry * 1.02f);
}

static void test_half_low_pass_keeps_bass_and_kills_treble(void)
{
    uint16_t half_lp = AUDIO_FILTER_RAW_CENTER / 2u;
    float bass = rms_after_filter(100.0f, half_lp, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);
    float treble = rms_after_filter(8000.0f, half_lp, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    CHECK(bass > normal_bass * 0.85f);
    CHECK(treble < normal_treble * 0.15f);
}

static void test_full_low_pass_kills_mids_and_most_bass(void)
{
    float mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_MIN, true);
    float normal_mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_MIN, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);

    CHECK(mids < normal_mids * 0.10f);
    CHECK(bass < normal_bass * 0.75f);
}

static void test_low_pass_treble_cut_deepens_monotonically(void)
{
    float normal = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float quarter = rms_after_filter(8000.0f,
                                     AUDIO_FILTER_RAW_CENTER -
                                     (AUDIO_FILTER_RAW_CENTER / 4u), true);
    float half = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER / 2u, true);
    float full = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MIN, true);

    CHECK(quarter < normal * 0.75f);
    CHECK(half < quarter * 0.5f);
    CHECK(full < half * 0.5f);
}

static void test_half_high_pass_kills_bass_and_keeps_treble(void)
{
    uint16_t half_hp = AUDIO_FILTER_RAW_CENTER +
                       ((AUDIO_FILTER_RAW_MAX - AUDIO_FILTER_RAW_CENTER) / 2u);
    float bass = rms_after_filter(100.0f, half_hp, true);
    float normal_bass = rms_after_filter(100.0f, AUDIO_FILTER_RAW_CENTER, true);
    float treble = rms_after_filter(8000.0f, half_hp, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    CHECK(bass < normal_bass * 0.20f);
    CHECK(treble > normal_treble * 0.85f);
}

static void test_full_high_pass_kills_mids_and_keeps_some_treble(void)
{
    float mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_MAX, true);
    float normal_mids = rms_after_filter(1000.0f, AUDIO_FILTER_RAW_CENTER, true);
    float treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_MAX, true);
    float normal_treble = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    CHECK(mids < normal_mids * 0.15f);
    CHECK(treble > normal_treble * 0.70f);
}

static void test_resonant_bump_lifts_tone_at_cutoff(void)
{
    /* raw chosen so the low-pass cutoff lands near 8 kHz:
     * intensity = ln(18000/8000) / ln(18000/60) ~= 0.142. */
    uint16_t raw = (uint16_t)(AUDIO_FILTER_RAW_CENTER -
                              (uint16_t)(0.142f * (float)AUDIO_FILTER_RAW_CENTER));
    float at_cutoff = rms_after_filter(8000.0f, raw, true);
    float dry = rms_after_filter(8000.0f, AUDIO_FILTER_RAW_CENTER, true);

    CHECK(at_cutoff > dry * 1.05f);
    CHECK(at_cutoff < dry * 1.60f);
}

/* Recover the cutoff the filter actually programmed. The ZDF SVF stores
 * g = tan(pi*fc/fs) split across a1 = 1/(1+g(g+k)) and a2 = g*a1, so g = a2/a1
 * and fc = atan(g)*fs/pi. Nothing about the cutoff is exposed directly, but it
 * is fully determined by coefficients that are. */
static float programmed_cutoff_hz(const audio_filter_state_t *filter)
{
    const float g = filter->a2 / filter->a1;
    return atanf(g) * (float)TEST_SAMPLE_RATE / 3.14159265358979323846f;
}

static void settle_coefficients(audio_filter_state_t *filter, uint16_t raw)
{
    audio_filter_set_raw(filter, raw);
    /* The smoothed knob approaches the target geometrically and refreshes once
     * per 32-frame block, so give it enough blocks to snap. */
    for (uint32_t i = 0; i < TEST_SAMPLE_RATE; ++i) {
        (void)audio_filter_process_frame(filter, true, (audio_mixer_frame_t){ .left = 0, .right = 0 });
    }
}

/* The exponential knob map is driven by two precomputed log ratios standing in
 * for logf() calls that would otherwise run on every coefficient refresh. A
 * source gate can only assert that the token "logf(" is absent, which says
 * nothing about whether the constants replacing it are right - a typo in either
 * ratio keeps the gate green and silently moves the entire sweep.
 *
 * Check the curve the constants exist to produce: cutoff should be geometric in
 * knob travel, 18 kHz -> 60 Hz to the left and 20 Hz -> 8 kHz to the right,
 * which is what makes every degree of turn one musical interval. Sampling
 * several points pins the shape, not just the endpoints. */
static void test_knob_sweep_follows_the_documented_exponential_curve(void)
{
    static const float intensities[] = { 0.25f, 0.5f, 0.75f, 1.0f };

    for (unsigned i = 0; i < sizeof(intensities) / sizeof(intensities[0]); ++i) {
        const float t = intensities[i];
        const uint16_t travel = (uint16_t)(t * (float)AUDIO_FILTER_RAW_CENTER);

        /* Left of centre: low-pass, 18000 * (60/18000)^t. */
        audio_filter_state_t lp;
        audio_filter_init(&lp, TEST_SAMPLE_RATE);
        settle_coefficients(&lp, (uint16_t)(AUDIO_FILTER_RAW_CENTER - travel));
        CHECK(!lp.hp_mode);
        const float lp_expected = 18000.0f * powf(60.0f / 18000.0f, t);
        const float lp_actual = programmed_cutoff_hz(&lp);
        CHECK(fabsf(lp_actual - lp_expected) < lp_expected * 0.02f);

        /* Right of centre: high-pass, 20 * (8000/20)^t. */
        audio_filter_state_t hp;
        audio_filter_init(&hp, TEST_SAMPLE_RATE);
        settle_coefficients(&hp, (uint16_t)(AUDIO_FILTER_RAW_CENTER + travel));
        CHECK(hp.hp_mode);
        const float hp_expected = 20.0f * powf(8000.0f / 20.0f, t);
        const float hp_actual = programmed_cutoff_hz(&hp);
        CHECK(fabsf(hp_actual - hp_expected) < hp_expected * 0.02f);
    }
}

/* A stable knob must not re-run expf/tanf on every coefficient block. Comparing
 * coefficients before and after cannot show this - recomputing the same input
 * yields the same output - so poison one coefficient and see whether the filter
 * writes over it. Surviving poison proves the recompute was skipped; the second
 * half proves the skip is conditional and not simply broken. */
static void test_stable_knob_skips_coefficient_recomputation(void)
{
    audio_filter_state_t filter;
    audio_filter_init(&filter, TEST_SAMPLE_RATE);
    settle_coefficients(&filter, AUDIO_FILTER_RAW_MIN);
    CHECK(!filter.bypassed);

    const float poison = -12345.0f;
    filter.a1 = poison;
    for (uint32_t i = 0; i < 4096u; ++i) {
        (void)audio_filter_process_frame(&filter, true, (audio_mixer_frame_t){ .left = 0, .right = 0 });
    }
    CHECK(filter.a1 == poison);

    /* Moving the knob must reach the recompute the skip was guarding. */
    audio_filter_set_raw(&filter, AUDIO_FILTER_RAW_MAX);
    for (uint32_t i = 0; i < 4096u; ++i) {
        (void)audio_filter_process_frame(&filter, true, (audio_mixer_frame_t){ .left = 0, .right = 0 });
    }
    CHECK(filter.a1 != poison);
}

int main(void)
{
    test_knob_sweep_follows_the_documented_exponential_curve();
    test_stable_knob_skips_coefficient_recomputation();
    test_disabled_filter_is_bypass_even_with_extreme_raw();
    test_center_filter_is_bypass_when_enabled();
    test_half_low_pass_keeps_bass_and_kills_treble();
    test_full_low_pass_kills_mids_and_most_bass();
    test_low_pass_treble_cut_deepens_monotonically();
    test_half_high_pass_kills_bass_and_keeps_treble();
    test_full_high_pass_kills_mids_and_keeps_some_treble();
    test_resonant_bump_lifts_tone_at_cutoff();
    printf("TESTS_RUN=%u\n", s_checks);
    puts("audio_filter tests passed");
    return 0;
}
