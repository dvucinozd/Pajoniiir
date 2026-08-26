#include "audio_resampler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    const audio_mixer_frame_t *frames;
    uint32_t count;
    uint32_t index;
} source_t;

typedef struct {
    uint64_t popped;
} counting_source_t;

static bool pop_source(void *ctx, audio_mixer_frame_t *out_frame)
{
    source_t *source = (source_t *)ctx;
    if (source->index >= source->count) return false;
    *out_frame = source->frames[source->index++];
    return true;
}

static bool pop_counting_source(void *ctx, audio_mixer_frame_t *out_frame)
{
    counting_source_t *source = (counting_source_t *)ctx;
    source->popped++;
    out_frame->left = (int16_t)(source->popped & 0x3FFFu);
    out_frame->right = (int16_t)-out_frame->left;
    return true;
}

static void test_reset_outputs_silence_without_source(void)
{
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint32_t consumed = 99;
    audio_mixer_frame_t out = audio_resampler_next(&state, 1.0f, NULL, NULL, &consumed);

    assert(out.left == 0);
    assert(out.right == 0);
    assert(consumed == 0);
}

static void test_unity_pitch_preserves_existing_one_frame_latency(void)
{
    audio_mixer_frame_t frames[] = {
        { .left = 100, .right = -100 },
        { .left = 200, .right = -200 },
    };
    source_t source = { .frames = frames, .count = 2, .index = 0 };
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint32_t consumed = 0;
    audio_mixer_frame_t out = audio_resampler_next(&state, 1.0f, pop_source, &source, &consumed);
    assert(out.left == 0);
    assert(out.right == 0);
    assert(consumed == 1);

    out = audio_resampler_next(&state, 1.0f, pop_source, &source, &consumed);
    assert(out.left == 100);
    assert(out.right == -100);
    assert(consumed == 1);
}

static void test_fractional_pitch_interpolates_between_source_frames(void)
{
    audio_mixer_frame_t frames[] = {
        { .left = 100, .right = -100 },
    };
    source_t source = { .frames = frames, .count = 1, .index = 0 };
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint32_t consumed = 0;
    audio_mixer_frame_t out = audio_resampler_next(&state, 0.5f, pop_source, &source, &consumed);
    assert(out.left == 0);
    assert(out.right == 0);
    assert(consumed == 0);

    out = audio_resampler_next(&state, 0.5f, pop_source, &source, &consumed);
    assert(out.left == 0);
    assert(out.right == 0);
    assert(consumed == 1);

    out = audio_resampler_next(&state, 0.5f, pop_source, &source, &consumed);
    assert(out.left == 50);
    assert(out.right == -50);
    assert(consumed == 0);
}

/* Ring underrun (pop_source returns false after data has flowed) must hold the
 * last sample, not snap to zero — a snap to zero clicks. */
static void test_underrun_holds_last_frame(void)
{
    audio_mixer_frame_t frames[] = {
        { .left = 1000, .right = -1000 },
    };
    source_t source = { .frames = frames, .count = 1, .index = 0 };
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint32_t consumed = 0;
    /* Prime: pops the one frame into `current`; output is still the (zero)
     * previous frame at unity's one-frame latency. */
    audio_resampler_next(&state, 1.0f, pop_source, &source, &consumed);
    assert(consumed == 1);

    /* Underrun: source is exhausted, so the resampler holds the last frame. */
    audio_mixer_frame_t out = audio_resampler_next(&state, 1.0f, pop_source, &source, &consumed);
    assert(out.left == 1000);
    assert(out.right == -1000);
    assert(consumed == 0);

    /* Sustained underrun keeps holding (flat), never decays to zero. */
    out = audio_resampler_next(&state, 1.0f, pop_source, &source, &consumed);
    assert(out.left == 1000);
    assert(out.right == -1000);
    assert(consumed == 0);
}

static void test_non_finite_pitch_is_sanitized(void)
{
    audio_mixer_frame_t frames[] = {
        { .left = 123, .right = -123 },
    };
    source_t source = { .frames = frames, .count = 1, .index = 0 };
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint32_t consumed = 0;
    (void)audio_resampler_next(&state, NAN, pop_source, &source, &consumed);
    assert(consumed == 1u);
    assert(state.step_q32 == (UINT64_C(1) << 32));
}

static void assert_five_minute_consumption(float factor)
{
    const uint32_t output_frames = 5u * 60u * 48000u;
    counting_source_t source = { 0 };
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint64_t consumed_total = 0u;
    for (uint32_t i = 0u; i < output_frames; i++) {
        uint32_t consumed = 0u;
        (void)audio_resampler_next(&state, factor,
                                   pop_counting_source, &source, &consumed);
        consumed_total += consumed;
    }

    uint64_t expected = (uint64_t)((double)output_frames * (double)factor);
    assert(consumed_total == expected);
    assert(source.popped == expected);
}

static void test_five_minute_q32_phase_has_zero_consumption_drift(void)
{
    assert_five_minute_consumption(44100.0f / 48000.0f);
    assert_five_minute_consumption(1.1f);
}

static void test_pitch_change_refreshes_cached_q32_step(void)
{
    counting_source_t source = { 0 };
    audio_resampler_state_t state;
    audio_resampler_reset(&state);

    uint64_t consumed_total = 0u;
    for (uint32_t i = 0u; i < 10u; i++) {
        uint32_t consumed = 0u;
        (void)audio_resampler_next(&state, 0.5f,
                                   pop_counting_source, &source, &consumed);
        consumed_total += consumed;
    }
    for (uint32_t i = 0u; i < 10u; i++) {
        uint32_t consumed = 0u;
        (void)audio_resampler_next(&state, 1.5f,
                                   pop_counting_source, &source, &consumed);
        consumed_total += consumed;
    }
    assert(consumed_total == 20u);
    assert(source.popped == 20u);
}

int main(void)
{
    test_reset_outputs_silence_without_source();
    test_unity_pitch_preserves_existing_one_frame_latency();
    test_fractional_pitch_interpolates_between_source_frames();
    test_underrun_holds_last_frame();
    test_non_finite_pitch_is_sanitized();
    test_five_minute_q32_phase_has_zero_consumption_drift();
    test_pitch_change_refreshes_cached_q32_step();
    puts("audio_resampler tests passed");
    return 0;
}
