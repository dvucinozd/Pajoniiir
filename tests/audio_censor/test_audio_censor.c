#include "audio_censor.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    uint64_t first_seq;
    const audio_mixer_frame_t *frames;
    uint32_t count;
} test_timeline_t;

static bool test_read(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    test_timeline_t *timeline = ctx;
    if (!timeline || !out || seq < timeline->first_seq ||
        seq - timeline->first_seq >= timeline->count) {
        return false;
    }
    *out = timeline->frames[seq - timeline->first_seq];
    return true;
}

static bool nearf(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

static void test_reverse_walks_backward_at_unity_rate(void)
{
    const audio_mixer_frame_t frames[] = {
        {100, -100}, {200, -200}, {300, -300}, {400, -400}, {500, -500},
    };
    test_timeline_t timeline = {10u, frames, 5u};
    audio_censor_t censor;
    audio_censor_init(&censor);
    assert(audio_censor_begin(&censor, 14u, 48000u, 48000u, 1.0f, 4u));

    audio_mixer_frame_t out = {0};
    float gain = 0.0f;
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 500 && out.right == -500 && nearf(gain, 1.0f));
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 400 && out.right == -400);
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 300 && out.right == -300);
}

static void test_mixed_rate_reverse_interpolates_fractional_head(void)
{
    const audio_mixer_frame_t frames[] = {
        {100, 100}, {200, 200}, {300, 300}, {400, 400}, {500, 500},
    };
    test_timeline_t timeline = {20u, frames, 5u};
    audio_censor_t censor;
    assert(audio_censor_begin(&censor, 24u, 24000u, 48000u, 1.0f, 4u));

    audio_mixer_frame_t out = {0};
    float gain = 0.0f;
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 500);
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 450);
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 400);
}

static void test_release_crossfade_reaches_forward_without_seek(void)
{
    const audio_mixer_frame_t frames[] = {
        {100, 100}, {200, 200}, {300, 300}, {400, 400}, {500, 500},
    };
    test_timeline_t timeline = {30u, frames, 5u};
    audio_censor_t censor;
    assert(audio_censor_begin(&censor, 34u, 48000u, 48000u, 1.0f, 4u));
    audio_censor_release(&censor);

    const float expected[] = {1.0f, 0.75f, 0.5f, 0.25f};
    for (uint32_t i = 0u; i < 4u; i++) {
        audio_mixer_frame_t out = {0};
        float gain = 0.0f;
        assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
        assert(nearf(gain, expected[i]));
    }
    assert(!audio_censor_is_active(&censor));
}

static void test_bounded_history_edge_fades_once_to_silence(void)
{
    const audio_mixer_frame_t frames[] = {{6400, -6400}, {3200, -3200}};
    test_timeline_t timeline = {0u, frames, 2u};
    audio_censor_t censor;
    assert(audio_censor_begin(&censor, 1u, 48000u, 48000u, 1.0f, 4u));

    audio_mixer_frame_t out = {0};
    float gain = 0.0f;
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 3200);
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 6400);
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 6400);
    assert(censor.edge_hits == 1u && censor.exhausted);
    assert(audio_censor_render(&censor, test_read, &timeline, &out, &gain));
    assert(out.left == 6300);
    for (uint32_t i = 0u; i < AUDIO_CENSOR_EDGE_FADE_FRAMES; i++) {
        (void)audio_censor_render(&censor, test_read, &timeline, &out, &gain);
    }
    assert(out.left == 0 && out.right == 0);
    assert(censor.edge_hits == 1u);
}

static void test_invalid_configuration_is_rejected(void)
{
    audio_censor_t censor;
    audio_censor_init(&censor);
    assert(!audio_censor_begin(&censor, 0u, 0u, 48000u, 1.0f, 4u));
    assert(!audio_censor_begin(&censor, 0u, 48000u, 0u, 1.0f, 4u));
    assert(!audio_censor_begin(&censor, 0u, 48000u, 48000u, 1.0f, 0u));
    assert(!audio_censor_is_active(&censor));
}

int main(void)
{
    test_reverse_walks_backward_at_unity_rate();
    test_mixed_rate_reverse_interpolates_fractional_head();
    test_release_crossfade_reaches_forward_without_seek();
    test_bounded_history_edge_fades_once_to_silence();
    test_invalid_configuration_is_rejected();
    puts("audio_censor tests passed");
    return 0;
}
