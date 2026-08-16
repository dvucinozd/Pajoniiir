#include "audio_pcm_timeline.h"

#include <stdio.h>
#include <stdlib.h>

/* Counted so tests/run_p4_host_tests.ps1 can pin how many assertions this suite
 * executes; a deleted test lowers the count and fails the run. Also replaces
 * CHECK(), which NDEBUG would compile away silently. */
static unsigned s_checks;
#define CHECK(expr) do {                                                     \
    s_checks++;                                                              \
    if (!(expr)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);      \
        abort();                                                             \
    }                                                                        \
} while (0)

#define CAP 6u
static int16_t s_storage[CAP * 2u];

static void push_value(audio_pcm_timeline_t *t, int16_t value)
{
    CHECK(audio_pcm_timeline_push(t, value, (int16_t)-value));
}

static void expect_seq(const audio_pcm_timeline_t *t, uint64_t seq, int16_t value)
{
    audio_mixer_frame_t frame = { 0 };
    CHECK(audio_pcm_timeline_read(t, seq, &frame));
    CHECK(frame.left == value);
    CHECK(frame.right == (int16_t)-value);
}

static void test_initial_future_and_normal_pop(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    CHECK(audio_pcm_timeline_generation(&t) != 0u);
    push_value(&t, 10);
    push_value(&t, 20);
    push_value(&t, 30);
    CHECK(audio_pcm_timeline_future_frames(&t) == 3u);
    CHECK(audio_pcm_timeline_history_frames(&t) == 0u);

    audio_mixer_frame_t frame = { 0 };
    CHECK(audio_pcm_timeline_pop(&t, &frame));
    CHECK(frame.left == 10);
    CHECK(audio_pcm_timeline_future_frames(&t) == 2u);
    CHECK(audio_pcm_timeline_history_frames(&t) == 1u);
}

static void test_full_cache_protects_unplayed_audio(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, (int16_t)(100 + i));
    CHECK(audio_pcm_timeline_used_frames(&t) == CAP);
    CHECK(!audio_pcm_timeline_push(&t, 999, -999));
    expect_seq(&t, 0u, 100);
}

static void test_consumed_history_is_evicted_on_wrap(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, i);

    audio_mixer_frame_t frame;
    CHECK(audio_pcm_timeline_pop(&t, &frame));
    CHECK(audio_pcm_timeline_pop(&t, &frame));
    CHECK(audio_pcm_timeline_pop(&t, &frame));
    CHECK(audio_pcm_timeline_history_frames(&t) == 3u);

    push_value(&t, 6);
    push_value(&t, 7);
    CHECK(audio_pcm_timeline_oldest_seq(&t) == 2u);
    CHECK(!audio_pcm_timeline_read(&t, 1u, &frame));
    expect_seq(&t, 2u, 2);
    expect_seq(&t, 6u, 6);
    expect_seq(&t, 7u, 7);
}

static void test_reposition_playhead_inside_history_and_future(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < 5; i++) push_value(&t, (int16_t)(i * 10));
    CHECK(audio_pcm_timeline_set_playhead(&t, 3u));
    CHECK(audio_pcm_timeline_history_frames(&t) == 3u);
    CHECK(audio_pcm_timeline_future_frames(&t) == 2u);

    audio_mixer_frame_t frame;
    CHECK(audio_pcm_timeline_pop(&t, &frame));
    CHECK(frame.left == 30);

    /* Newest seq=4; two frames back is seq=2. */
    CHECK(audio_pcm_timeline_set_playhead_frames_back(&t, 2u));
    CHECK(audio_pcm_timeline_pop(&t, &frame));
    CHECK(frame.left == 20);
    CHECK(!audio_pcm_timeline_set_playhead(&t, 6u));
}

static void test_reset_changes_generation_and_drops_all_cursors(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    push_value(&t, 1);
    uint32_t before = audio_pcm_timeline_generation(&t);
    audio_pcm_timeline_reset(&t);
    CHECK(audio_pcm_timeline_generation(&t) != before);
    CHECK(audio_pcm_timeline_used_frames(&t) == 0u);
    CHECK(audio_pcm_timeline_future_frames(&t) == 0u);
    CHECK(audio_pcm_timeline_history_frames(&t) == 0u);
}

static void test_pop_cursor_crosses_physical_wrap(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, i);

    audio_mixer_frame_t frame;
    for (int16_t i = 0; i < 4; i++) {
        CHECK(audio_pcm_timeline_pop(&t, &frame));
        CHECK(frame.left == i);
    }
    for (int16_t i = 6; i < 10; i++) push_value(&t, i);
    for (int16_t i = 4; i < 10; i++) {
        CHECK(audio_pcm_timeline_pop(&t, &frame));
        CHECK(frame.left == i);
    }
    CHECK(!audio_pcm_timeline_pop(&t, &frame));
}

static void test_random_read_derives_slot_from_sequence_after_many_evictions(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t i = 0; i < (int16_t)CAP; i++) push_value(&t, i);

    audio_mixer_frame_t frame;
    for (int16_t i = 0; i < 4; i++) {
        CHECK(audio_pcm_timeline_pop(&t, &frame));
        push_value(&t, (int16_t)(CAP + i));
    }

    CHECK(audio_pcm_timeline_oldest_seq(&t) == 4u);
    for (uint64_t seq = 4u; seq < 10u; seq++) {
        expect_seq(&t, seq, (int16_t)seq);
    }
}

/* The read path anchors on play_seq/play_index - cursors owned by the same task
 * that reads - rather than on a physical index for the oldest frame, which the
 * producer moves as it evicts. Reading retained frames correctly is only half of
 * that property: a reader carrying its own eviction index also has to *refuse*
 * the sequences that left the window, and refusing them is what stops evicted
 * PCM from being handed to the key-lock path as if it were still live.
 *
 * Every slot here still holds plausible audio - eviction only advances a cursor,
 * it does not clear the storage - so a reader that indexed physically instead of
 * by sequence would return a confidently wrong frame rather than fail. */
static void test_random_read_rejects_sequences_outside_the_retained_window(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);

    audio_mixer_frame_t frame;
    /* Drive well past a single wrap so oldest_seq and the physical ring are out
     * of phase, which is when a stale physical index goes wrong. */
    for (int16_t i = 0; i < (int16_t)(CAP * 3u); i++) {
        if (i >= (int16_t)CAP) {
            CHECK(audio_pcm_timeline_pop(&t, &frame));
        }
        push_value(&t, i);
    }

    const uint64_t oldest = audio_pcm_timeline_oldest_seq(&t);
    const uint64_t write = audio_pcm_timeline_write_seq(&t);
    CHECK(oldest > 0u);          /* frames really were evicted */
    CHECK(write - oldest <= CAP);

    /* Everything inside the window still reads back its own value. */
    for (uint64_t seq = oldest; seq < write; seq++) {
        expect_seq(&t, seq, (int16_t)seq);
    }

    /* The frame just evicted must be refused, not served from its old slot. */
    CHECK(!audio_pcm_timeline_read(&t, oldest - 1u, &frame));
    /* As must one a full lap back, whose slot now holds a live frame. */
    CHECK(!audio_pcm_timeline_read(&t, oldest - CAP, &frame));
    /* And the not-yet-written end, whose slot holds the oldest retained frame. */
    CHECK(!audio_pcm_timeline_read(&t, write, &frame));
    CHECK(!audio_pcm_timeline_read(&t, write + 1u, &frame));
}

/* A loop wrap has to withdraw decoded frames that fell outside the loop. The
 * decoder runs ~2 s ahead of playback, so without this the audio past the loop
 * out point is already published and plays before the loop's first pass. */
static void test_drop_newest_withdraws_only_the_unplayed_runway(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t v = 1; v <= 5; ++v) push_value(&t, v);

    audio_mixer_frame_t out;
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 1);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 2);

    /* 3,4,5 are the runway; asking for more than that must clamp to it and
     * must not claw back 1 and 2, which playback has already taken. */
    CHECK(audio_pcm_timeline_drop_newest(&t, 99u) == 3u);
    CHECK(audio_pcm_timeline_write_seq(&t) == 2u);
    CHECK(audio_pcm_timeline_play_seq(&t) == 2u);
    CHECK(!audio_pcm_timeline_pop(&t, &out));

    /* History below play_seq survives, so scratch keeps its window. */
    CHECK(audio_pcm_timeline_read(&t, 0u, &out) && out.left == 1);
    CHECK(audio_pcm_timeline_read(&t, 1u, &out) && out.left == 2);

    /* The store stays usable: the next push lands where the withdrawn frame
     * was, and playback picks it up rather than replaying a stale slot. */
    push_value(&t, 42);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 42);
}

static void test_drop_newest_partial_keeps_the_frames_still_inside_the_loop(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    for (int16_t v = 1; v <= 5; ++v) push_value(&t, v);

    /* Beat-loop case: the out point is ahead of the playhead, so only the tail
     * is outside the loop. Everything before it must survive. */
    CHECK(audio_pcm_timeline_drop_newest(&t, 2u) == 2u);
    CHECK(audio_pcm_timeline_write_seq(&t) == 3u);

    audio_mixer_frame_t out;
    for (int16_t v = 1; v <= 3; ++v) {
        CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == v);
    }
    CHECK(!audio_pcm_timeline_pop(&t, &out));
    CHECK(audio_pcm_timeline_drop_newest(&t, 1u) == 0u);   /* nothing left */
    CHECK(audio_pcm_timeline_drop_newest(&t, 0u) == 0u);
    CHECK(audio_pcm_timeline_drop_newest(NULL, 1u) == 0u);
}

/* The physical write cursor must rewind across the buffer wrap, not clamp at
 * zero — otherwise the next push overwrites the wrong slot. */
static void test_drop_newest_rewinds_across_the_physical_wrap(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, CAP);
    audio_mixer_frame_t out;
    for (int16_t v = 1; v <= 5; ++v) {
        push_value(&t, v);
        CHECK(audio_pcm_timeline_pop(&t, &out));
    }
    /* write_index now sits at 5; pushing two more wraps it to 1. */
    push_value(&t, 10);
    push_value(&t, 11);
    CHECK(t.write_index == 1u);

    CHECK(audio_pcm_timeline_drop_newest(&t, 2u) == 2u);
    CHECK(t.write_index == 5u);

    push_value(&t, 20);
    push_value(&t, 21);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 20);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 21);
}

static void test_capacity_rejects_ambiguous_modular_span(void)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, s_storage, 0x80000000u);
    CHECK(t.capacity == 0u);
    CHECK(!audio_pcm_timeline_push(&t, 1, -1));
}

static void seed_empty_at(audio_pcm_timeline_t *t, uint64_t sequence)
{
    audio_pcm_timeline_init(t, s_storage, CAP);
    t->oldest_seq = (uint32_t)sequence;
    t->play_seq = (uint32_t)sequence;
    t->write_seq = (uint32_t)sequence;
    t->oldest_epoch = (uint32_t)(sequence >> 32);
    t->play_epoch = (uint32_t)(sequence >> 32);
    t->write_epoch = (uint32_t)(sequence >> 32);
}

static void test_all_cursors_and_random_reads_cross_uint32_wrap(void)
{
    const uint64_t base = (uint64_t)UINT32_MAX - 2u;
    audio_pcm_timeline_t t;
    seed_empty_at(&t, base);

    push_value(&t, 10);
    push_value(&t, 20);
    push_value(&t, 30);
    push_value(&t, 40);
    CHECK(audio_pcm_timeline_write_seq(&t) == base + 4u);
    CHECK(audio_pcm_timeline_future_frames(&t) == 4u);
    expect_seq(&t, base, 10);
    expect_seq(&t, base + 1u, 20);
    expect_seq(&t, base + 2u, 30);
    expect_seq(&t, base + 3u, 40);

    audio_mixer_frame_t out;
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 10);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 20);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 30);
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 40);
    CHECK(audio_pcm_timeline_play_seq(&t) == base + 4u);
    CHECK(audio_pcm_timeline_history_frames(&t) == 4u);

    /* Fill beyond capacity after consumption so oldest_seq also crosses its
     * low-word wrap while retained random reads stay coherent. */
    push_value(&t, 50);
    push_value(&t, 60);
    push_value(&t, 70);
    push_value(&t, 80);
    push_value(&t, 90);
    CHECK(audio_pcm_timeline_oldest_seq(&t) == (1ull << 32));
    CHECK(audio_pcm_timeline_used_frames(&t) == CAP);
    expect_seq(&t, 1ull << 32, 40);
    expect_seq(&t, (1ull << 32) + 1u, 50);
    expect_seq(&t, (1ull << 32) + 5u, 90);
    CHECK(!audio_pcm_timeline_read(&t, (1ull << 32) - 1u, &out));

    CHECK(audio_pcm_timeline_set_playhead(&t, (1ull << 32) + 3u));
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 70);
    CHECK(audio_pcm_timeline_set_playhead_frames_back(&t, 1u));
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 80);
}

static void test_drop_newest_crosses_uint32_wrap_backwards(void)
{
    const uint64_t base = (uint64_t)UINT32_MAX - 1u;
    audio_pcm_timeline_t t;
    seed_empty_at(&t, base);
    push_value(&t, 1);
    push_value(&t, 2);
    push_value(&t, 3);
    push_value(&t, 4);

    audio_mixer_frame_t out;
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 1);
    CHECK(audio_pcm_timeline_write_seq(&t) == (1ull << 32) + 2u);
    CHECK(audio_pcm_timeline_drop_newest(&t, 3u) == 3u);
    CHECK(audio_pcm_timeline_write_seq(&t) == (uint64_t)UINT32_MAX);
    CHECK(audio_pcm_timeline_play_seq(&t) == (uint64_t)UINT32_MAX);
    CHECK(audio_pcm_timeline_future_frames(&t) == 0u);

    push_value(&t, 42);
    CHECK(audio_pcm_timeline_write_seq(&t) == (1ull << 32));
    CHECK(audio_pcm_timeline_pop(&t, &out) && out.left == 42);
}

int main(void)
{
    test_drop_newest_withdraws_only_the_unplayed_runway();
    test_drop_newest_partial_keeps_the_frames_still_inside_the_loop();
    test_drop_newest_rewinds_across_the_physical_wrap();
    test_initial_future_and_normal_pop();
    test_capacity_rejects_ambiguous_modular_span();
    test_full_cache_protects_unplayed_audio();
    test_consumed_history_is_evicted_on_wrap();
    test_reposition_playhead_inside_history_and_future();
    test_reset_changes_generation_and_drops_all_cursors();
    test_pop_cursor_crosses_physical_wrap();
    test_random_read_derives_slot_from_sequence_after_many_evictions();
    test_random_read_rejects_sequences_outside_the_retained_window();
    test_all_cursors_and_random_reads_cross_uint32_wrap();
    test_drop_newest_crosses_uint32_wrap_backwards();
    printf("TESTS_RUN=%u\n", s_checks);
    puts("audio_pcm_timeline tests passed");
    return 0;
}
