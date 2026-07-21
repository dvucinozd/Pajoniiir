/*
 * Host tests for the P4 master-recorder SPSC ring and writer-drain core.
 *
 * Single-threaded functional coverage: push/peek/consume ordering, wrap-around
 * data integrity, full-ring drop accounting, high-water tracking, and the
 * writer draining to a fake storage sink including the mid-drain failure path.
 */
#include "audio_recorder_ring.h"
#include "audio_recorder_writer.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                 \
            s_failures++;                                                      \
        }                                                                      \
    } while (0)

/* Fill a stereo block buffer with a recognizable ramp keyed by `seed`. */
static void fill_block(int16_t *stereo, uint32_t frames, int seed)
{
    for (uint32_t i = 0; i < frames * 2u; i++) {
        stereo[i] = (int16_t)(seed * 1000 + (int)i);
    }
}

static void test_ring_basic(void)
{
    printf("== ring push/peek/consume ==\n");
    audio_recorder_block_t slots[4];
    audio_recorder_ring_t ring;
    audio_recorder_ring_init(&ring, slots, 4u);

    CHECK(audio_recorder_ring_used(&ring) == 0u);
    CHECK(audio_recorder_ring_peek(&ring) == NULL);

    int16_t blk[AUDIO_RECORDER_BLOCK_SAMPLES];
    for (int i = 0; i < 3; i++) {
        fill_block(blk, AUDIO_RECORDER_BLOCK_FRAMES, i + 1);
        CHECK(audio_recorder_ring_push(&ring, blk, AUDIO_RECORDER_BLOCK_FRAMES, 48000u));
    }
    CHECK(audio_recorder_ring_used(&ring) == 3u);
    CHECK(ring.pushed_blocks == 3u);
    CHECK(ring.high_water == 3u);

    const audio_recorder_block_t *first = audio_recorder_ring_peek(&ring);
    CHECK(first != NULL);
    CHECK(first->frames == AUDIO_RECORDER_BLOCK_FRAMES);
    CHECK(first->sample_rate == 48000u);
    CHECK(first->samples[0] == (int16_t)1000);   /* seed 1 */
    audio_recorder_ring_consume(&ring);
    CHECK(audio_recorder_ring_used(&ring) == 2u);

    /* Next block is seed 2. */
    const audio_recorder_block_t *second = audio_recorder_ring_peek(&ring);
    CHECK(second != NULL && second->samples[0] == (int16_t)2000);
}

static void test_ring_full_drop(void)
{
    printf("== ring full-drop accounting ==\n");
    audio_recorder_block_t slots[2];
    audio_recorder_ring_t ring;
    audio_recorder_ring_init(&ring, slots, 2u);

    int16_t blk[AUDIO_RECORDER_BLOCK_SAMPLES];
    fill_block(blk, 128u, 7);
    CHECK(audio_recorder_ring_push(&ring, blk, 128u, 44100u));   /* clamps kept: 128 <= 256 */
    CHECK(audio_recorder_ring_push(&ring, blk, 128u, 44100u));
    CHECK(audio_recorder_ring_used(&ring) == 2u);

    /* Ring is full: the next push must drop without blocking. */
    CHECK(!audio_recorder_ring_push(&ring, blk, 128u, 44100u));
    CHECK(ring.dropped_blocks == 1u);
    CHECK(ring.dropped_frames == 128u);
    CHECK(ring.pushed_blocks == 2u);

    /* Frame count is preserved on a partial (<256) block. */
    const audio_recorder_block_t *b = audio_recorder_ring_peek(&ring);
    CHECK(b != NULL && b->frames == 128u);
}

static void test_ring_wrap_integrity(void)
{
    printf("== ring wrap integrity ==\n");
    audio_recorder_block_t slots[3];
    audio_recorder_ring_t ring;
    audio_recorder_ring_init(&ring, slots, 3u);

    int16_t blk[AUDIO_RECORDER_BLOCK_SAMPLES];
    /* Push and immediately consume 10 blocks so the physical slots wrap. */
    for (int i = 0; i < 10; i++) {
        fill_block(blk, 64u, i + 1);
        CHECK(audio_recorder_ring_push(&ring, blk, 64u, 48000u));
        const audio_recorder_block_t *b = audio_recorder_ring_peek(&ring);
        CHECK(b != NULL);
        CHECK(b->frames == 64u);
        CHECK(b->samples[0] == (int16_t)((i + 1) * 1000));
        CHECK(b->samples[64u * 2u - 1u] == (int16_t)((i + 1) * 1000 + (int)(64u * 2u - 1u)));
        audio_recorder_ring_consume(&ring);
    }
    CHECK(audio_recorder_ring_used(&ring) == 0u);
    CHECK(ring.dropped_blocks == 0u);
}

/* ── fake storage sink ─────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  buf[64 * 1024];
    size_t   len;
    size_t   fail_at_bytes;   /* fail once total would exceed this (0 = never) */
} fake_sink_t;

static int fake_sink_write(void *ctx, const void *data, size_t len)
{
    fake_sink_t *s = (fake_sink_t *)ctx;
    if (s->fail_at_bytes != 0u && s->len + len > s->fail_at_bytes) {
        return -1;
    }
    if (s->len + len > sizeof(s->buf)) {
        return -1;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    return (int)len;
}

static void test_writer_drain_all(void)
{
    printf("== writer drain (all) ==\n");
    audio_recorder_block_t slots[8];
    audio_recorder_ring_t ring;
    audio_recorder_ring_init(&ring, slots, 8u);

    int16_t blk[AUDIO_RECORDER_BLOCK_SAMPLES];
    for (int i = 0; i < 3; i++) {
        fill_block(blk, 100u, i + 1);
        CHECK(audio_recorder_ring_push(&ring, blk, 100u, 48000u));
    }

    fake_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    audio_recorder_drain_result_t res =
        audio_recorder_writer_drain(&ring, fake_sink_write, &sink, 16u);

    CHECK(res.blocks_written == 3u);
    CHECK(!res.sink_error);
    CHECK(res.bytes_written == 3u * 100u * 4u);
    CHECK(sink.len == 3u * 100u * 4u);
    CHECK(audio_recorder_ring_used(&ring) == 0u);   /* all consumed */

    /* Draining an empty ring is a no-op. */
    audio_recorder_drain_result_t empty =
        audio_recorder_writer_drain(&ring, fake_sink_write, &sink, 16u);
    CHECK(empty.blocks_written == 0u && !empty.sink_error);
}

static void test_writer_drain_error(void)
{
    printf("== writer drain (sink error mid-batch) ==\n");
    audio_recorder_block_t slots[8];
    audio_recorder_ring_t ring;
    audio_recorder_ring_init(&ring, slots, 8u);

    int16_t blk[AUDIO_RECORDER_BLOCK_SAMPLES];
    for (int i = 0; i < 3; i++) {
        fill_block(blk, 100u, i + 1);
        CHECK(audio_recorder_ring_push(&ring, blk, 100u, 48000u));
    }

    fake_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    sink.fail_at_bytes = 100u * 4u + 1u;   /* first block OK, second fails */

    audio_recorder_drain_result_t res =
        audio_recorder_writer_drain(&ring, fake_sink_write, &sink, 16u);

    CHECK(res.blocks_written == 1u);
    CHECK(res.sink_error);
    CHECK(res.bytes_written == 100u * 4u);
    /* The failing block is left unconsumed so the caller can react. */
    CHECK(audio_recorder_ring_used(&ring) == 2u);
    const audio_recorder_block_t *stuck = audio_recorder_ring_peek(&ring);
    CHECK(stuck != NULL && stuck->samples[0] == (int16_t)2000);   /* seed 2 */
}

static void test_writer_max_blocks(void)
{
    printf("== writer respects max_blocks ==\n");
    audio_recorder_block_t slots[8];
    audio_recorder_ring_t ring;
    audio_recorder_ring_init(&ring, slots, 8u);

    int16_t blk[AUDIO_RECORDER_BLOCK_SAMPLES];
    for (int i = 0; i < 5; i++) {
        fill_block(blk, 32u, i + 1);
        CHECK(audio_recorder_ring_push(&ring, blk, 32u, 48000u));
    }

    fake_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    audio_recorder_drain_result_t res =
        audio_recorder_writer_drain(&ring, fake_sink_write, &sink, 2u);
    CHECK(res.blocks_written == 2u);
    CHECK(audio_recorder_ring_used(&ring) == 3u);
}

int main(void)
{
    printf("=== audio_recorder_pipeline tests ===\n");
    test_ring_basic();
    test_ring_full_drop();
    test_ring_wrap_integrity();
    test_writer_drain_all();
    test_writer_drain_error();
    test_writer_max_blocks();

    if (s_failures == 0) {
        printf("audio_recorder_pipeline tests passed\n");
        return 0;
    }
    printf("audio_recorder_pipeline tests FAILED (%d)\n", s_failures);
    return 1;
}
