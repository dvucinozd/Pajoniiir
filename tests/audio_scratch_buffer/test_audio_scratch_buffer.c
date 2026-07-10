#include "audio_scratch_buffer.h"
#include <assert.h>
#include <stdio.h>

/* Backing store big enough for the largest capacity used below (8 frames). */
static int16_t g_store[8 * 2];

static void test_reset_starts_empty(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 8u);

    assert(audio_scratch_buffer_used(&b) == 0u);
    uint32_t idx = 12345u;
    /* No frames + no newest mark -> mapping fails. */
    assert(!audio_scratch_buffer_index_for_ms(&b, 0u, &idx));
}

static void test_push_and_used_caps_at_capacity(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 4u);

    for (int i = 0; i < 3; i++) {
        audio_scratch_buffer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    assert(audio_scratch_buffer_used(&b) == 3u);

    /* Overfill: used saturates at capacity. */
    for (int i = 3; i < 10; i++) {
        audio_scratch_buffer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    assert(audio_scratch_buffer_used(&b) == 4u);
}

static void test_read_bounds(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 4u);
    audio_scratch_buffer_push(&b, 111, -111);

    int16_t l = 0, r = 0;
    assert(audio_scratch_buffer_read(&b, 0u, &l, &r));
    assert(l == 111 && r == -111);
    /* Out-of-range index rejected. */
    assert(!audio_scratch_buffer_read(&b, 4u, &l, &r));
}

/* sample_rate = 1000 makes ms<->frame a 1:1 mapping for readable assertions. */
static void test_index_for_ms_maps_positions(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 8u);
    audio_scratch_buffer_set_sample_rate(&b, 1000u);

    for (int i = 0; i < 8; i++) {
        audio_scratch_buffer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    /* Newest frame (value 8) is at track position 200 ms. */
    audio_scratch_buffer_mark_newest_ms(&b, 200u);

    uint32_t idx = 0;
    int16_t l = 0, r = 0;

    /* Newest position -> newest frame. */
    assert(audio_scratch_buffer_index_for_ms(&b, 200u, &idx));
    assert(audio_scratch_buffer_read(&b, idx, &l, &r));
    assert(l == 8 && r == -8);

    /* One ms back -> previous frame. */
    assert(audio_scratch_buffer_index_for_ms(&b, 199u, &idx));
    assert(audio_scratch_buffer_read(&b, idx, &l, &r));
    assert(l == 7 && r == -7);

    /* Oldest in-window frame (8 frames -> 193..200 ms). */
    assert(audio_scratch_buffer_index_for_ms(&b, 193u, &idx));
    assert(audio_scratch_buffer_read(&b, idx, &l, &r));
    assert(l == 1 && r == -1);

    /* Older than the window -> reject. */
    assert(!audio_scratch_buffer_index_for_ms(&b, 192u, &idx));
    /* Future (newer than newest) -> reject. */
    assert(!audio_scratch_buffer_index_for_ms(&b, 201u, &idx));
}

/* After wrap the newest/oldest mapping must still track the live window. */
static void test_wrap_preserves_window_mapping(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 4u);
    audio_scratch_buffer_set_sample_rate(&b, 1000u);

    /* Push 6 frames into a 4-frame store: values 3,4,5,6 survive. */
    for (int i = 0; i < 6; i++) {
        audio_scratch_buffer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    assert(audio_scratch_buffer_used(&b) == 4u);
    audio_scratch_buffer_mark_newest_ms(&b, 1000u);

    uint32_t idx = 0;
    int16_t l = 0, r = 0;

    assert(audio_scratch_buffer_index_for_ms(&b, 1000u, &idx));
    assert(audio_scratch_buffer_read(&b, idx, &l, &r));
    assert(l == 6 && r == -6);   /* newest */

    assert(audio_scratch_buffer_index_for_ms(&b, 997u, &idx));
    assert(audio_scratch_buffer_read(&b, idx, &l, &r));
    assert(l == 3 && r == -3);   /* oldest survivor */

    assert(!audio_scratch_buffer_index_for_ms(&b, 996u, &idx));  /* evicted */
}

static void test_reset_after_seek_clears_window(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 8u);
    audio_scratch_buffer_set_sample_rate(&b, 1000u);
    for (int i = 0; i < 8; i++) {
        audio_scratch_buffer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    audio_scratch_buffer_mark_newest_ms(&b, 200u);

    audio_scratch_buffer_reset(&b);
    assert(audio_scratch_buffer_used(&b) == 0u);
    uint32_t idx = 0;
    assert(!audio_scratch_buffer_index_for_ms(&b, 200u, &idx));
    /* Sample rate survives a reset (only the window is dropped). */
    assert(b.sample_rate == 1000u);
}

static void test_null_and_unset_guards(void)
{
    uint32_t idx = 0;
    int16_t l = 0, r = 0;
    assert(audio_scratch_buffer_used(NULL) == 0u);
    assert(!audio_scratch_buffer_index_for_ms(NULL, 0u, &idx));
    assert(!audio_scratch_buffer_read(NULL, 0u, &l, &r));

    /* Filled but no sample rate set -> mapping fails. */
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 4u);
    audio_scratch_buffer_push(&b, 1, -1);
    audio_scratch_buffer_mark_newest_ms(&b, 10u);
    assert(!audio_scratch_buffer_index_for_ms(&b, 10u, &idx));
}

int main(void)
{
    test_reset_starts_empty();
    test_push_and_used_caps_at_capacity();
    test_read_bounds();
    test_index_for_ms_maps_positions();
    test_wrap_preserves_window_mapping();
    test_reset_after_seek_clears_window();
    test_null_and_unset_guards();
    puts("audio_scratch_buffer tests passed");
    return 0;
}
