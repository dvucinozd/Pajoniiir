#include "audio_pcm_timeline.h"

#include <assert.h>
#include <stdio.h>

#define CAP 6u
static int16_t s_storage[CAP * 2u];

static void push_value(audio_pcm_timeline_t *t, int16_t value)
{
    assert(audio_pcm_timeline_push(t, value, (int16_t)-value));
}

static void expect_seq(const audio_pcm_timeline_t *t, uint64_t seq, int16_t value)
{
    audio_mixer_frame_t frame = { 0 };
    assert(audio_pcm_timeline_read(t, seq, &frame));
    assert(frame.left == value);
    assert(frame.right == (int16_t)-value);
}

static void test_initial_future_and_normal_pop(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    assert(audio_pcm_timeline_generation(&t) != 0u);
    push_value(&t, 10);
    push_value(&t, 20);
    push_value(&t, 30);
    assert(audio_pcm_timeline_future_frames(&t) == 3u);
    assert(audio_pcm_timeline_history_frames(&t) == 0u);

    audio_mixer_frame_t frame = { 0 };
    assert(audio_pcm_timeline_pop(&t, &frame));
    assert(frame.left == 10);
    assert(audio_pcm_timeline_future_frames(&t) == 2u);
    assert(audio_pcm_timeline_history_frames(&t) == 1u);
}

static void test_full_cache_protects_unplayed_audio(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, (int16_t)(100 + i));
    assert(audio_pcm_timeline_used_frames(&t) == CAP);
    assert(!audio_pcm_timeline_push(&t, 999, -999));
    expect_seq(&t, 0u, 100);
}

static void test_consumed_history_is_evicted_on_wrap(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, i);

    audio_mixer_frame_t frame;
    assert(audio_pcm_timeline_pop(&t, &frame));
    assert(audio_pcm_timeline_pop(&t, &frame));
    assert(audio_pcm_timeline_pop(&t, &frame));
    assert(audio_pcm_timeline_history_frames(&t) == 3u);

    push_value(&t, 6);
    push_value(&t, 7);
    assert(audio_pcm_timeline_oldest_seq(&t) == 2u);
    assert(!audio_pcm_timeline_read(&t, 1u, &frame));
    expect_seq(&t, 2u, 2);
    expect_seq(&t, 6u, 6);
    expect_seq(&t, 7u, 7);
}

static void test_reposition_playhead_inside_history_and_future(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < 5; i++) push_value(&t, (int16_t)(i * 10));
    assert(audio_pcm_timeline_set_playhead(&t, 3u));
    assert(audio_pcm_timeline_history_frames(&t) == 3u);
    assert(audio_pcm_timeline_future_frames(&t) == 2u);

    audio_mixer_frame_t frame;
    assert(audio_pcm_timeline_pop(&t, &frame));
    assert(frame.left == 30);

    /* Newest seq=4; two frames back is seq=2. */
    assert(audio_pcm_timeline_set_playhead_frames_back(&t, 2u));
    assert(audio_pcm_timeline_pop(&t, &frame));
    assert(frame.left == 20);
    assert(!audio_pcm_timeline_set_playhead(&t, 6u));
}

static void test_reset_changes_generation_and_drops_all_cursors(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    push_value(&t, 1);
    uint32_t before = audio_pcm_timeline_generation(&t);
    audio_pcm_timeline_reset(&t);
    assert(audio_pcm_timeline_generation(&t) != before);
    assert(audio_pcm_timeline_used_frames(&t) == 0u);
    assert(audio_pcm_timeline_future_frames(&t) == 0u);
    assert(audio_pcm_timeline_history_frames(&t) == 0u);
}

static void test_pop_cursor_crosses_physical_wrap(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, i);

    audio_mixer_frame_t frame;
    for (int16_t i = 0; i < 4; i++) {
        assert(audio_pcm_timeline_pop(&t, &frame));
        assert(frame.left == i);
    }
    for (int16_t i = 6; i < 10; i++) push_value(&t, i);
    for (int16_t i = 4; i < 10; i++) {
        assert(audio_pcm_timeline_pop(&t, &frame));
        assert(frame.left == i);
    }
    assert(!audio_pcm_timeline_pop(&t, &frame));
}

static void test_random_read_derives_slot_from_sequence_after_many_evictions(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, i);

    audio_mixer_frame_t frame;
    for (int16_t i = 0; i < 4; i++) {
        assert(audio_pcm_timeline_pop(&t, &frame));
        push_value(&t, (int16_t)(CAP + i));
    }

    assert(audio_pcm_timeline_oldest_seq(&t) == 4u);
    for (uint64_t seq = 4u; seq < 10u; seq++) {
        expect_seq(&t, seq, (int16_t)seq);
    }
}

int main(void)
{
    test_initial_future_and_normal_pop();
    test_full_cache_protects_unplayed_audio();
    test_consumed_history_is_evicted_on_wrap();
    test_reposition_playhead_inside_history_and_future();
    test_reset_changes_generation_and_drops_all_cursors();
    test_pop_cursor_crosses_physical_wrap();
    test_random_read_derives_slot_from_sequence_after_many_evictions();
    puts("audio_pcm_timeline tests passed");
    return 0;
}
