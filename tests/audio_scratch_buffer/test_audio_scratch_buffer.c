#include "audio_scratch_buffer.h"
#include <assert.h>
#include <stdio.h>

static int16_t g_store[8 * 2];

/* Mirrors the sole production writer's ring bookkeeping. The buffer component
 * intentionally exposes only reset/state accessors and frames-back reads. */
static void test_writer_push(audio_scratch_buffer_t *b, int16_t left, int16_t right)
{
    assert(b && b->frames && b->capacity > 0u);
    uint32_t idx = b->write_index;
    b->frames[idx * 2u] = left;
    b->frames[idx * 2u + 1u] = right;
    b->write_index = idx + 1u < b->capacity ? idx + 1u : 0u;
    if (b->filled < b->capacity) b->filled++;
}

static void test_reset_and_generation(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 8u);
    assert(audio_scratch_buffer_used(&b) == 0u);
    assert(audio_scratch_buffer_generation(&b) != 0u);

    uint32_t generation = audio_scratch_buffer_generation(&b);
    test_writer_push(&b, 1, -1);
    audio_scratch_buffer_set_sample_rate(&b, 48000u);
    audio_scratch_buffer_mark_newest_ms(&b, 1234u);
    audio_scratch_buffer_reset(&b);

    assert(audio_scratch_buffer_used(&b) == 0u);
    assert(audio_scratch_buffer_generation(&b) != generation);
    assert(b.sample_rate == 48000u);
    assert(!b.newest_valid);
}

static void test_frames_back_reads_live_window_after_wrap(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 4u);
    for (int i = 0; i < 6; ++i) {
        test_writer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    assert(audio_scratch_buffer_used(&b) == 4u);

    int16_t left = 0;
    int16_t right = 0;
    assert(audio_scratch_buffer_read_frame_back(&b, 0u, &left, &right));
    assert(left == 6 && right == -6);
    assert(audio_scratch_buffer_read_frame_back(&b, 3u, &left, &right));
    assert(left == 3 && right == -3);
    assert(!audio_scratch_buffer_read_frame_back(&b, 4u, &left, &right));
}

static void test_frames_back_guards(void)
{
    int16_t left = 0;
    int16_t right = 0;
    assert(audio_scratch_buffer_used(NULL) == 0u);
    assert(audio_scratch_buffer_generation(NULL) == 0u);
    assert(!audio_scratch_buffer_read_frame_back(NULL, 0u, &left, &right));

    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, NULL, 4u);
    assert(!audio_scratch_buffer_read_frame_back(&b, 0u, &left, &right));
}

int main(void)
{
    test_reset_and_generation();
    test_frames_back_reads_live_window_after_wrap();
    test_frames_back_guards();
    puts("audio_scratch_buffer tests passed");
    return 0;
}
