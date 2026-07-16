#include "audio_delay_fx.h"
#include "audio_output_mixer.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    const audio_mixer_frame_t *frames;
    uint32_t count;
    uint32_t index;
} source_t;

static bool pop_source(void *ctx, audio_mixer_frame_t *out_frame)
{
    source_t *source = (source_t *)ctx;
    if (source->index >= source->count) return false;
    *out_frame = source->frames[source->index++];
    return true;
}

static audio_output_mix_result_t mix_full(const audio_output_mixer_deck_t *deck0,
                                          const audio_output_mixer_deck_t *deck1,
                                          bool deck0_pfl,
                                          bool deck1_pfl,
                                          audio_output_headphone_mode_t headphone_mode,
                                          uint16_t headphone_mix,
                                          bool master_cue_enabled,
                                          uint32_t *out_deck0_consumed,
                                          uint32_t *out_deck1_consumed,
                                          audio_mixer_limiter_stats_t *limiter_stats)
{
    return audio_output_mixer_next_full_with_headphone_level(deck0, deck1,
        deck0_pfl, deck1_pfl, headphone_mode, headphone_mix,
        AUDIO_MIXER_CONTROL_MAX, master_cue_enabled,
        out_deck0_consumed, out_deck1_consumed, limiter_stats);
}

static audio_mixer_frame_t mix_master(const audio_output_mixer_deck_t *deck0,
                                      const audio_output_mixer_deck_t *deck1,
                                      uint32_t *out_deck0_consumed,
                                      uint32_t *out_deck1_consumed,
                                      audio_mixer_limiter_stats_t *limiter_stats)
{
    return mix_full(deck0, deck1, false, false,
                    AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                    AUDIO_MIXER_CONTROL_MAX, true,
                    out_deck0_consumed, out_deck1_consumed,
                    limiter_stats).master;
}

static void test_mixes_two_active_decks_with_output_gains(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 10000, .right = 10000 },
        { .left = 10000, .right = -10000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 20000, .right = 20000 },
        { .left = 20000, .right = 20000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_resampler_reset(&deck0_resampler);
    audio_resampler_reset(&deck1_resampler);

    audio_output_mixer_deck_t deck0 = {
        .active = true,
        .pitch_factor = 1.0f,
        .gain = 1.0f,
        .resampler = &deck0_resampler,
        .pop_source = pop_source,
        .source_ctx = &deck0_source,
    };
    audio_output_mixer_deck_t deck1 = {
        .active = true,
        .pitch_factor = 1.0f,
        .gain = 0.25f,
        .resampler = &deck1_resampler,
        .pop_source = pop_source,
        .source_ctx = &deck1_source,
    };
    uint32_t consumed0 = 0;
    uint32_t consumed1 = 0;

    mix_master(&deck0, &deck1, &consumed0, &consumed1, NULL);
    audio_mixer_frame_t out = mix_master(&deck0, &deck1, &consumed0, &consumed1, NULL);

    assert(out.left == 15000);
    assert(out.right == 15000);
    assert(consumed0 == 1);
    assert(consumed1 == 1);
}

static void test_inactive_deck_does_not_consume_source(void)
{
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 32000, .right = 32000 },
    };
    source_t deck1_source = { .frames = deck1_frames, .count = 1, .index = 0 };
    audio_resampler_state_t deck1_resampler;
    audio_resampler_reset(&deck1_resampler);

    audio_output_mixer_deck_t deck1 = {
        .active = false,
        .pitch_factor = 1.0f,
        .gain = 1.0f,
        .resampler = &deck1_resampler,
        .pop_source = pop_source,
        .source_ctx = &deck1_source,
    };
    uint32_t consumed0 = 99;
    uint32_t consumed1 = 99;

    audio_mixer_frame_t out = mix_master(NULL, &deck1, &consumed0, &consumed1, NULL);

    assert(out.left == 0);
    assert(out.right == 0);
    assert(consumed0 == 0);
    assert(consumed1 == 0);
    assert(deck1_source.index == 0);
}

static void test_deck_sample_rate_ratio_affects_source_consumption(void)
{
    audio_mixer_frame_t frames[200];
    for (size_t i = 0; i < 200; i++) {
        frames[i] = (audio_mixer_frame_t){ .left = 1000, .right = 1000 };
    }
    source_t source = { .frames = frames, .count = 200, .index = 0 };
    audio_resampler_state_t resampler;
    audio_resampler_reset(&resampler);
    audio_output_mixer_deck_t deck = {
        .active = true,
        .pitch_factor = 1.0f,
        .source_sample_rate = 48000,
        .output_sample_rate = 44100,
        .gain = 1.0f,
        .resampler = &resampler,
        .pop_source = pop_source,
        .source_ctx = &source,
    };

    uint32_t total_consumed = 0;
    for (int i = 0; i < 147; i++) {
        uint32_t consumed = 0;
        mix_master(&deck, NULL, &consumed, NULL, NULL);
        total_consumed += consumed;
    }

    assert(total_consumed == 160);
    assert(source.index == 160);
}

static audio_output_mixer_deck_t make_deck(source_t *source,
                                           audio_resampler_state_t *resampler,
                                           float gain)
{
    audio_resampler_reset(resampler);
    return (audio_output_mixer_deck_t) {
        .active = true,
        .pitch_factor = 1.0f,
        .gain = gain,
        .resampler = resampler,
        .pop_source = pop_source,
        .source_ctx = source,
    };
}

static void prime_output_mixer(audio_output_mixer_deck_t *deck0,
                               audio_output_mixer_deck_t *deck1)
{
    uint32_t consumed0 = 0;
    uint32_t consumed1 = 0;
    mix_master(deck0, deck1, &consumed0, &consumed1, NULL);
}

static void test_master_limiter_leaves_single_deck_unchanged(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 30000, .right = -30000 },
        { .left = 30000, .right = -30000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_mixer_limiter_stats_t stats = { 0 };

    prime_output_mixer(&deck0, NULL);
    audio_mixer_frame_t out = mix_master(&deck0, NULL, NULL, NULL, &stats);

    assert(out.left == 30000);
    assert(out.right == -30000);
    assert(stats.limited_samples == 0);
    assert(stats.positive_overloads == 0);
    assert(stats.negative_overloads == 0);
}

static void test_master_limiter_leaves_normal_two_deck_sum_unchanged(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 10000, .right = -10000 },
        { .left = 10000, .right = -10000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 12000, .right = -12000 },
        { .left = 12000, .right = -12000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);
    audio_mixer_limiter_stats_t stats = { 0 };

    prime_output_mixer(&deck0, &deck1);
    audio_mixer_frame_t out = mix_master(&deck0, &deck1, NULL, NULL, &stats);

    assert(out.left == 22000);
    assert(out.right == -22000);
    assert(stats.limited_samples == 0);
}

static void test_deck_gain_allows_pregain_boost_before_limiter(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 9000, .right = -9000 },
        { .left = 9000, .right = -9000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 2.0f);
    audio_mixer_limiter_stats_t stats = { 0 };

    prime_output_mixer(&deck0, NULL);
    audio_mixer_frame_t out = mix_master(&deck0, NULL, NULL, NULL, &stats);

    assert(out.left == 18000);
    assert(out.right == -18000);
    assert(stats.limited_samples == 0);
}

static void test_master_limiter_shapes_overloads_and_reports_telemetry(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 30000, .right = -30000 },
        { .left = 30000, .right = -30000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 30000, .right = -30000 },
        { .left = 30000, .right = -30000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);
    audio_mixer_limiter_stats_t stats = { 0 };

    prime_output_mixer(&deck0, &deck1);
    audio_mixer_frame_t out = mix_master(&deck0, &deck1, NULL, NULL, &stats);

    assert(out.left <= 32767);
    assert(out.left > 30000);
    assert(out.right >= -32768);
    assert(out.right < -30000);
    assert(stats.limited_samples == 2);
    assert(stats.positive_overloads == 1);
    assert(stats.negative_overloads == 1);
    assert(stats.peak_input_abs == 60000);
}

static void test_full_mix_keeps_master_stereo_when_cue_is_enabled(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 1000, .right = 3000 },
        { .left = 1000, .right = 3000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 7000, .right = 9000 },
        { .left = 7000, .right = 9000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t out = mix_full(&deck0, &deck1,
                                                                 false, true,
                                                                 AUDIO_OUTPUT_HEADPHONE_CUE_MONO,
                                                                 AUDIO_MIXER_CONTROL_MAX,
                                                                 true,
                                                                 NULL, NULL, NULL);

    assert(out.master.left == 8000);
    assert(out.master.right == 12000);
    assert(out.headphone.left == 8000);
    assert(out.headphone.right == 8000);
}

static void test_full_mix_split_monitor_uses_master_left_and_pfl_right(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 1000, .right = 3000 },
        { .left = 1000, .right = 3000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 7000, .right = 9000 },
        { .left = 7000, .right = 9000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t out = mix_full(&deck0, &deck1,
                                                                 false, true,
                                                                 AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO,
                                                                 AUDIO_MIXER_CONTROL_MAX,
                                                                 true,
                                                                 NULL, NULL, NULL);

    assert(out.master.left == 8000);
    assert(out.master.right == 12000);
    assert(out.headphone.left == 10000);
    assert(out.headphone.right == 8000);
}

static void test_full_mix_headphone_mix_blends_cue_to_stereo_master(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 2000, .right = 6000 },
        { .left = 2000, .right = 6000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 10000, .right = 10000 },
        { .left = 10000, .right = 10000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 0.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t out = mix_full(&deck0, &deck1,
                                                                 false, true,
                                                                 AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                                                 AUDIO_MIXER_CONTROL_CENTER,
                                                                 true,
                                                                 NULL, NULL, NULL);

    assert(out.master.left == 2000);
    assert(out.master.right == 6000);
    assert(out.headphone.left == 6000);
    assert(out.headphone.right == 8000);
}

static void test_full_mix_master_cue_disabled_removes_master_from_headphone_mix_only(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 2000, .right = 6000 },
        { .left = 2000, .right = 6000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 10000, .right = 10000 },
        { .left = 10000, .right = 10000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 0.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t out = mix_full(&deck0, &deck1,
                                                                 false, true,
                                                                 AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                                                 AUDIO_MIXER_CONTROL_MAX,
                                                                 false,
                                                                 NULL, NULL, NULL);

    assert(out.master.left == 2000);
    assert(out.master.right == 6000);
    assert(out.headphone.left == 0);
    assert(out.headphone.right == 0);
}

static void test_beat_fx_filter_applies_only_to_target_deck(void)
{
    audio_mixer_frame_t deck0_frames[64];
    audio_mixer_frame_t deck1_frames[64];
    for (size_t i = 0; i < 64; i++) {
        int16_t sample = (i & 1u) ? 12000 : -12000;
        deck0_frames[i] = (audio_mixer_frame_t){ .left = sample, .right = sample };
        deck1_frames[i] = (audio_mixer_frame_t){ .left = sample, .right = sample };
    }

    source_t deck0_source = { .frames = deck0_frames, .count = 64, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 64, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_filter_state_t deck0_beat_fx;
    audio_filter_state_t deck1_beat_fx;
    audio_filter_init(&deck0_beat_fx, 44100u);
    audio_filter_init(&deck1_beat_fx, 44100u);
    audio_filter_set_raw(&deck0_beat_fx, AUDIO_FILTER_RAW_MIN);
    audio_filter_set_raw(&deck1_beat_fx, AUDIO_FILTER_RAW_MIN);
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);
    deck0.beat_fx_filter = &deck0_beat_fx;
    deck0.beat_fx_filter_enabled = true;
    deck1.beat_fx_filter = &deck1_beat_fx;
    deck1.beat_fx_filter_enabled = false;

    int64_t target_abs = 0;
    int64_t bypass_abs = 0;
    for (int i = 0; i < 64; i++) {
        audio_output_mix_result_t out = mix_full(&deck0, &deck1,
                                                                     false, false,
                                                                     AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                                                     AUDIO_MIXER_CONTROL_MAX,
                                                                     true,
                                                                     NULL, NULL, NULL);
        target_abs += out.deck_frame[0].left < 0 ? -out.deck_frame[0].left : out.deck_frame[0].left;
        bypass_abs += out.deck_frame[1].left < 0 ? -out.deck_frame[1].left : out.deck_frame[1].left;
    }

    assert(target_abs < bypass_abs / 2);
    assert(bypass_abs > 64 * 10000);
}

static void assert_beat_fx_time_effect_applies_only_to_target_deck(
    audio_delay_fx_mode_t mode,
    uint16_t feedback_q15)
{
    audio_mixer_frame_t deck0_frames[8] = {
        { .left = 10000, .right = 10000 },
    };
    audio_mixer_frame_t deck1_frames[8] = {
        { .left = 7000, .right = 7000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 8, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 8, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    int16_t effect_l[16] = { 0 };
    int16_t effect_r[16] = { 0 };
    audio_delay_fx_t effect;
    audio_delay_fx_init(&effect, effect_l, effect_r, 16u, 1000u);
    audio_delay_fx_configure(&effect, &(audio_delay_fx_config_t) {
        .enabled = true,
        .mode = mode,
        .delay_ms = 2,
        .wet_q15 = 16384,
        .feedback_q15 = feedback_q15,
    });
    deck0.beat_fx_echo = &effect;
    deck0.beat_fx_echo_enabled = true;

    prime_output_mixer(&deck0, &deck1);
    (void)mix_full(&deck0, &deck1, false, false,
                                       AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                       AUDIO_MIXER_CONTROL_MAX,
                                       true,
                                       NULL, NULL, NULL);
    (void)mix_full(&deck0, &deck1, false, false,
                                       AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                       AUDIO_MIXER_CONTROL_MAX,
                                       true,
                                       NULL, NULL, NULL);
    audio_output_mix_result_t delayed = mix_full(&deck0, &deck1,
                                                                     false, false,
                                                                     AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                                                     AUDIO_MIXER_CONTROL_MAX,
                                                                     true,
                                                                     NULL, NULL, NULL);

    assert(delayed.deck_frame[0].left > 0);
    assert(delayed.deck_frame[1].left == 0);
}

static void test_beat_fx_echo_applies_only_to_target_deck(void)
{
    assert_beat_fx_time_effect_applies_only_to_target_deck(
        AUDIO_DELAY_FX_MODE_ECHO,
        8192u);
}

static void test_beat_fx_delay_applies_only_to_target_deck(void)
{
    /* DELAY must suppress this non-zero caller feedback internally. */
    assert_beat_fx_time_effect_applies_only_to_target_deck(
        AUDIO_DELAY_FX_MODE_DELAY,
        20000u);
}

static void test_pad_fx_applies_before_beat_fx_layer(void)
{
    audio_mixer_frame_t deck0_frames[64];
    for (size_t i = 0; i < 64; i++) {
        int16_t sample = (i & 1u) ? 12000 : -12000;
        deck0_frames[i] = (audio_mixer_frame_t){ .left = sample, .right = sample };
    }

    source_t deck0_source = { .frames = deck0_frames, .count = 64, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_pad_fx_state_t pad_fx;
    audio_pad_fx_init(&pad_fx, 44100u);
    audio_pad_fx_set(&pad_fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX2,
        .pad = 0,
        .active = true,
    });
    deck0.pad_fx = &pad_fx;

    int64_t processed_abs = 0;
    for (int i = 0; i < 64; i++) {
        audio_output_mix_result_t out = mix_full(&deck0, NULL,
                                                                     false, false,
                                                                     AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                                                     AUDIO_MIXER_CONTROL_MAX,
                                                                     true,
                                                                     NULL, NULL, NULL);
        processed_abs += out.deck_frame[0].left < 0 ? -out.deck_frame[0].left : out.deck_frame[0].left;
    }

    assert(processed_abs < 64 * 12000);
    assert(audio_pad_fx_is_active(&pad_fx));
}

static bool scratch_yields_fixed(void *ctx, audio_mixer_frame_t *out)
{
    const int16_t *v = (const int16_t *)ctx;
    out->left = v[0];
    out->right = v[1];
    return true;
}

static bool scratch_yields_silence(void *ctx, audio_mixer_frame_t *out)
{
    (void)ctx;
    out->left = 0;
    out->right = 0;
    return false;
}

/* Vinyl mode Phase 4: an active scratch source replaces the resampler+ring for
 * that deck and consumes nothing from the ring. */
static void test_scratch_source_replaces_ring_without_consuming(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 100, .right = 100 },
        { .left = 100, .right = 100 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_reset(&deck0_resampler);
    int16_t scratch_val[2] = { 7000, -7000 };

    audio_output_mixer_deck_t deck0 = {
        .active = true,
        .pitch_factor = 1.0f,
        .gain = 1.0f,
        .resampler = &deck0_resampler,
        .pop_source = pop_source,
        .source_ctx = &deck0_source,
        .scratch_active = true,
        .scratch_render = scratch_yields_fixed,
        .scratch_ctx = scratch_val,
    };
    audio_output_mixer_deck_t deck1 = { .active = false };
    uint32_t consumed0 = 99;
    uint32_t consumed1 = 99;

    audio_mixer_frame_t out = mix_master(&deck0, &deck1,
                                         &consumed0, &consumed1, NULL);
    assert(out.left == 7000);
    assert(out.right == -7000);
    assert(consumed0 == 0);            /* scratch consumes no ring frames */
    assert(deck0_source.index == 0);   /* the ring source was never popped */
}

/* A scratch source that returns silence yields a silent deck frame. */
static void test_scratch_source_silence(void)
{
    audio_resampler_state_t deck0_resampler;
    audio_resampler_reset(&deck0_resampler);
    audio_output_mixer_deck_t deck0 = {
        .active = true,
        .pitch_factor = 1.0f,
        .gain = 1.0f,
        .resampler = &deck0_resampler,
        .scratch_active = true,
        .scratch_render = scratch_yields_silence,
        .scratch_ctx = NULL,
    };
    audio_output_mixer_deck_t deck1 = { .active = false };
    uint32_t consumed0 = 0;
    uint32_t consumed1 = 0;

    audio_mixer_frame_t out = mix_master(&deck0, &deck1,
                                         &consumed0, &consumed1, NULL);
    assert(out.left == 0);
    assert(out.right == 0);
}

int main(void)
{
    test_mixes_two_active_decks_with_output_gains();
    test_scratch_source_replaces_ring_without_consuming();
    test_scratch_source_silence();
    test_inactive_deck_does_not_consume_source();
    test_deck_sample_rate_ratio_affects_source_consumption();
    test_master_limiter_leaves_single_deck_unchanged();
    test_master_limiter_leaves_normal_two_deck_sum_unchanged();
    test_deck_gain_allows_pregain_boost_before_limiter();
    test_master_limiter_shapes_overloads_and_reports_telemetry();
    test_full_mix_keeps_master_stereo_when_cue_is_enabled();
    test_full_mix_split_monitor_uses_master_left_and_pfl_right();
    test_full_mix_headphone_mix_blends_cue_to_stereo_master();
    test_full_mix_master_cue_disabled_removes_master_from_headphone_mix_only();
    test_beat_fx_filter_applies_only_to_target_deck();
    test_beat_fx_echo_applies_only_to_target_deck();
    test_beat_fx_delay_applies_only_to_target_deck();
    test_pad_fx_applies_before_beat_fx_layer();
    puts("audio_output_mixer tests passed");
    return 0;
}
