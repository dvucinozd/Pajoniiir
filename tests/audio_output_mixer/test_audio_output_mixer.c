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

    audio_output_mixer_next(&deck0, &deck1, &consumed0, &consumed1, NULL);
    audio_mixer_frame_t out = audio_output_mixer_next(&deck0, &deck1, &consumed0, &consumed1, NULL);

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

    audio_mixer_frame_t out = audio_output_mixer_next(NULL, &deck1, &consumed0, &consumed1, NULL);

    assert(out.left == 0);
    assert(out.right == 0);
    assert(consumed0 == 0);
    assert(consumed1 == 0);
    assert(deck1_source.index == 0);
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
    audio_output_mixer_next(deck0, deck1, &consumed0, &consumed1, NULL);
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
    audio_mixer_frame_t out = audio_output_mixer_next(&deck0, NULL, NULL, NULL, &stats);

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
    audio_mixer_frame_t out = audio_output_mixer_next(&deck0, &deck1, NULL, NULL, &stats);

    assert(out.left == 22000);
    assert(out.right == -22000);
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
    audio_mixer_frame_t out = audio_output_mixer_next(&deck0, &deck1, NULL, NULL, &stats);

    assert(out.left <= 32767);
    assert(out.left > 30000);
    assert(out.right >= -32768);
    assert(out.right < -30000);
    assert(stats.limited_samples == 2);
    assert(stats.positive_overloads == 1);
    assert(stats.negative_overloads == 1);
    assert(stats.peak_input_abs == 60000);
}

int main(void)
{
    test_mixes_two_active_decks_with_output_gains();
    test_inactive_deck_does_not_consume_source();
    test_master_limiter_leaves_single_deck_unchanged();
    test_master_limiter_leaves_normal_two_deck_sum_unchanged();
    test_master_limiter_shapes_overloads_and_reports_telemetry();
    puts("audio_output_mixer tests passed");
    return 0;
}
