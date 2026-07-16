#include "audio_mixer.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static void test_fader_gain_clamps_to_unit_range(void)
{
    assert(audio_mixer_fader_gain(0) == 0.0f);
    assert(audio_mixer_fader_gain(AUDIO_MIXER_CONTROL_MAX) == 1.0f);
    assert(audio_mixer_fader_gain(AUDIO_MIXER_CONTROL_CENTER) > 0.49f);
    assert(audio_mixer_fader_gain(AUDIO_MIXER_CONTROL_CENTER) < 0.51f);
}

static void test_crossfader_keeps_center_both_decks_open(void)
{
    float deck1 = 0.0f;
    float deck2 = 0.0f;

    audio_mixer_crossfader_gains(0, &deck1, &deck2);
    assert(deck1 == 1.0f);
    assert(deck2 == 0.0f);

    audio_mixer_crossfader_gains(AUDIO_MIXER_CONTROL_CENTER, &deck1, &deck2);
    assert(deck1 == 1.0f);
    assert(deck2 == 1.0f);

    audio_mixer_crossfader_gains(AUDIO_MIXER_CONTROL_MAX, &deck1, &deck2);
    assert(deck1 == 0.0f);
    assert(deck2 == 1.0f);

    audio_mixer_crossfader_gains(UINT16_MAX, &deck1, &deck2);
    assert(deck1 == 0.0f);
    assert(deck2 == 1.0f);
}

static void test_mixer_saturates_instead_of_wrapping(void)
{
    assert(audio_mixer_mix_sample(20000, 20000, 1.0f, 1.0f) == 32767);
    assert(audio_mixer_mix_sample(-20000, -20000, 1.0f, 1.0f) == -32768);
    assert(audio_mixer_mix_sample(10000, -4000, 0.5f, 0.25f) == 4000);
}

static void test_stereo_frame_uses_channel_and_crossfader_gains(void)
{
    audio_mixer_frame_t out = audio_mixer_mix_stereo(
        (audio_mixer_frame_t){ .left = 10000, .right = -10000 },
        (audio_mixer_frame_t){ .left = 10000, .right = 10000 },
        1.0f,
        1.0f,
        AUDIO_MIXER_CONTROL_CENTER);

    assert(out.left == 20000);
    assert(out.right == 0);

    out = audio_mixer_mix_stereo(
        (audio_mixer_frame_t){ .left = 12000, .right = 12000 },
        (audio_mixer_frame_t){ .left = 30000, .right = 30000 },
        1.0f,
        1.0f,
        0);

    assert(out.left == 12000);
    assert(out.right == 12000);
}

static void test_apply_gain_scales_stereo_frame(void)
{
    audio_mixer_frame_t out = audio_mixer_apply_gain(
        (audio_mixer_frame_t){ .left = 12000, .right = -8000 },
        0.5f);

    assert(out.left == 6000);
    assert(out.right == -4000);

    out = audio_mixer_apply_gain(
        (audio_mixer_frame_t){ .left = 12000, .right = -8000 },
        -1.0f);

    assert(out.left == 0);
    assert(out.right == 0);
}

static void test_master_limiter_soft_knee_is_transparent_until_hot_peak(void)
{
    audio_mixer_limiter_stats_t stats = { 0 };

    assert(audio_mixer_limit_master_sample(29999, &stats) == 29999);
    assert(audio_mixer_limit_master_sample(-29999, &stats) == -29999);
    assert(stats.limited_samples == 0);
    assert(stats.peak_input_abs == 29999);

    int16_t hot_pos = audio_mixer_limit_master_sample(32000, &stats);
    int16_t hot_neg = audio_mixer_limit_master_sample(-32000, &stats);

    assert(hot_pos > 29999);
    assert(hot_pos < 32000);
    assert(hot_neg < -29999);
    assert(hot_neg > -32000);
    assert(stats.limited_samples == 2);
    assert(stats.positive_overloads == 1);
    assert(stats.negative_overloads == 1);
    assert(stats.peak_input_abs == 32000);
}

static void test_master_limiter_handles_full_int32_domain(void)
{
    audio_mixer_limiter_stats_t stats = { 0 };

    assert(audio_mixer_limit_master_sample(INT32_MAX, &stats) == 32767);
    assert(audio_mixer_limit_master_sample(INT32_MIN, &stats) == -32768);
    assert(stats.limited_samples == 2u);
    assert(stats.positive_overloads == 1u);
    assert(stats.negative_overloads == 1u);
    assert(stats.peak_input_abs == INT32_MAX);
}

static void test_non_finite_gain_mutes_instead_of_invoking_undefined_conversion(void)
{
    assert(audio_mixer_mix_sample(12000, 0, NAN, 0.0f) == 0);
}

int main(void)
{
    test_fader_gain_clamps_to_unit_range();
    test_crossfader_keeps_center_both_decks_open();
    test_mixer_saturates_instead_of_wrapping();
    test_stereo_frame_uses_channel_and_crossfader_gains();
    test_apply_gain_scales_stereo_frame();
    test_master_limiter_soft_knee_is_transparent_until_hot_peak();
    test_master_limiter_handles_full_int32_domain();
    test_non_finite_gain_mutes_instead_of_invoking_undefined_conversion();
    puts("audio_mixer tests passed");
    return 0;
}
