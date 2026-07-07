#include "p4_audio_link.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1; \
        } \
    } while (0)

#define EXPECT_EQ_U32(actual, expected, msg) \
    do { \
        uint32_t actual_ = (uint32_t)(actual); \
        uint32_t expected_ = (uint32_t)(expected); \
        if (actual_ != expected_) { \
            fprintf(stderr, "FAIL: %s got=%u expected=%u\n", msg, (unsigned)actual_, (unsigned)expected_); \
            return 1; \
        } \
    } while (0)

static uint32_t test_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t test_block_crc32(const p4_audio_link_block_header_t *header,
                                 const int16_t *frames,
                                 size_t payload_bytes)
{
    p4_audio_link_block_header_t protected_header = *header;
    protected_header.block_crc32 = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    crc = test_crc32_update(crc, (const uint8_t *)&protected_header, sizeof(protected_header));
    crc = test_crc32_update(crc, (const uint8_t *)frames, payload_bytes);
    return ~crc;
}

static size_t build_block(uint8_t *dst,
                          size_t dst_len,
                          uint32_t sequence,
                          uint32_t sample_rate,
                          const int16_t *frames,
                          uint16_t frame_count,
                          uint32_t crc_override)
{
    const size_t payload_bytes = (size_t)frame_count * 2u * sizeof(int16_t);
    const size_t total = sizeof(p4_audio_link_block_header_t) + payload_bytes;
    if (dst_len < total) {
        return 0u;
    }

    p4_audio_link_block_header_t hdr = {
        .magic = P4_AUDIO_LINK_BLOCK_MAGIC,
        .header_bytes = (uint16_t)sizeof(p4_audio_link_block_header_t),
        .frames = frame_count,
        .sample_rate = sample_rate,
        .sequence = sequence,
    };
    hdr.block_crc32 = test_block_crc32(&hdr, frames, payload_bytes);
    if (crc_override != UINT32_MAX) {
        hdr.block_crc32 = crc_override;
    }

    memcpy(dst, &hdr, sizeof(hdr));
    memcpy(dst + sizeof(hdr), frames, payload_bytes);
    return total;
}

static int test_receives_valid_blocks_and_reads_fifo_frames(void)
{
    uint8_t block[128] = { 0 };
    const int16_t pcm[] = {
        100, -100,
        200, -200,
        300, -300,
        400, -400,
    };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");
    size_t block_len = build_block(block, sizeof(block), 1u, 48000u, pcm, 4u, UINT32_MAX);
    EXPECT_TRUE(block_len > 0u, "block built");
    EXPECT_TRUE(p4_audio_link_receive_block(block, block_len), "valid block accepted");

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.received_blocks, 1u, "received block counted");
    EXPECT_EQ_U32(stats.sequence_gaps, 0u, "first sequence does not count as gap");
    EXPECT_EQ_U32(stats.crc_errors, 0u, "no crc errors");
    EXPECT_EQ_U32(stats.sample_rate, 48000u, "sample rate captured");
    EXPECT_EQ_U32(stats.ring_frames, 4u, "ring frame count");
    EXPECT_TRUE(stats.ring_capacity_frames >= 4096u, "ring has at least 4096 frames");

    int16_t out[4 * 2] = { 0 };
    size_t got = p4_audio_link_read_frames(out, 4u);
    EXPECT_EQ_U32(got, 4u, "read returns requested frames when available");
    EXPECT_TRUE(memcmp(out, pcm, sizeof(pcm)) == 0, "read preserves FIFO PCM samples");

    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.ring_frames, 0u, "ring drained");
    return 0;
}

static int test_sequence_gap_and_crc_error_are_counted(void)
{
    uint8_t block[128] = { 0 };
    const int16_t pcm[] = {
        10, -10,
        20, -20,
    };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");
    size_t block_len = build_block(block, sizeof(block), 10u, 48000u, pcm, 2u, UINT32_MAX);
    EXPECT_TRUE(p4_audio_link_receive_block(block, block_len), "first block accepted");

    block_len = build_block(block, sizeof(block), 12u, 48000u, pcm, 2u, UINT32_MAX);
    EXPECT_TRUE(p4_audio_link_receive_block(block, block_len), "gap block accepted");

    block_len = build_block(block, sizeof(block), 13u, 48000u, pcm, 2u, 0x12345678u);
    EXPECT_TRUE(!p4_audio_link_receive_block(block, block_len), "bad crc rejected");

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.received_blocks, 2u, "only valid blocks counted");
    EXPECT_EQ_U32(stats.sequence_gaps, 1u, "one sequence gap counted");
    EXPECT_EQ_U32(stats.crc_errors, 1u, "crc error counted");
    return 0;
}

static int test_corrupted_header_is_rejected_before_sequence_tracking(void)
{
    uint8_t block[128] = { 0 };
    const int16_t pcm[] = {
        10, -10,
        20, -20,
    };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");
    size_t block_len = build_block(block, sizeof(block), 1u, 48000u, pcm, 2u, UINT32_MAX);
    EXPECT_TRUE(p4_audio_link_receive_block(block, block_len), "first block accepted");

    block_len = build_block(block, sizeof(block), 2u, 48000u, pcm, 2u, UINT32_MAX);
    p4_audio_link_block_header_t hdr = { 0 };
    memcpy(&hdr, block, sizeof(hdr));
    hdr.sequence = 130u;
    memcpy(block, &hdr, sizeof(hdr));
    EXPECT_TRUE(!p4_audio_link_receive_block(block, block_len), "header-corrupted block rejected");

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.received_blocks, 1u, "corrupted header not counted as received");
    EXPECT_EQ_U32(stats.sequence_gaps, 0u, "corrupted header does not create a sequence gap");
    EXPECT_EQ_U32(stats.crc_errors, 1u, "corrupted header counted as crc error");
    EXPECT_EQ_U32(stats.ring_frames, 2u, "only first block frames in ring");
    return 0;
}

static int test_underrun_returns_silence(void)
{
    int16_t out[3 * 2] = { 1, 1, 1, 1, 1, 1 };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");
    size_t got = p4_audio_link_read_frames(out, 3u);
    EXPECT_EQ_U32(got, 0u, "empty ring returns zero real frames");
    for (size_t i = 0; i < 6u; ++i) {
        EXPECT_EQ_U32(out[i], 0u, "underrun clears destination to silence");
    }

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.underruns, 1u, "underrun counted");
    return 0;
}

static int test_overrun_drops_oldest_frames(void)
{
    uint8_t block[64] = { 0 };
    int16_t pcm[2] = { 0 };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");
    for (uint32_t i = 0; i < P4_AUDIO_LINK_RING_CAPACITY_FRAMES + 8u; ++i) {
        pcm[0] = (int16_t)i;
        pcm[1] = (int16_t)(-((int)i));
        size_t block_len = build_block(block, sizeof(block), i + 1u, 48000u, pcm, 1u, UINT32_MAX);
        EXPECT_TRUE(p4_audio_link_receive_block(block, block_len), "single-frame block accepted");
    }

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.ring_frames, P4_AUDIO_LINK_RING_CAPACITY_FRAMES, "ring remains capped");
    EXPECT_EQ_U32(stats.overruns, 8u, "oldest frames dropped on overrun");

    int16_t first[2] = { 0 };
    EXPECT_EQ_U32(p4_audio_link_read_frames(first, 1u), 1u, "can read first retained frame");
    EXPECT_EQ_U32((uint16_t)first[0], 8u, "oldest eight frames were dropped");
    return 0;
}

static int test_deframer_reassembles_chunked_stream_with_filler(void)
{
    uint8_t stream[1024] = { 0 };
    size_t pos = 0u;
    const int16_t pcm_a[] = { 100, -100, 200, -200, 300, -300, 400, -400 };
    const int16_t pcm_b[] = { 500, -500, 600, -600 };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");

    pos += 37u; /* leading zero filler, deliberately not 4-byte aligned */
    size_t len = build_block(&stream[pos], sizeof(stream) - pos, 1u, 48000u, pcm_a, 4u, UINT32_MAX);
    EXPECT_TRUE(len > 0u, "block A built");
    pos += len;
    pos += 21u; /* inter-block zero filler */
    len = build_block(&stream[pos], sizeof(stream) - pos, 2u, 48000u, pcm_b, 2u, UINT32_MAX);
    EXPECT_TRUE(len > 0u, "block B built");
    pos += len;
    pos += 16u; /* trailing filler */

    for (size_t off = 0u; off < pos; off += 7u) {
        size_t chunk = pos - off < 7u ? pos - off : 7u;
        p4_audio_link_feed_bytes(&stream[off], chunk);
    }

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.received_blocks, 2u, "both chunked blocks received");
    EXPECT_EQ_U32(stats.sequence_gaps, 0u, "no gaps across filler");
    EXPECT_EQ_U32(stats.crc_errors, 0u, "no crc errors");
    EXPECT_EQ_U32(stats.ring_frames, 6u, "all frames in ring");

    int16_t out[6 * 2] = { 0 };
    EXPECT_EQ_U32(p4_audio_link_read_frames(out, 6u), 6u, "frames readable");
    EXPECT_TRUE(memcmp(out, pcm_a, sizeof(pcm_a)) == 0, "block A PCM preserved");
    EXPECT_TRUE(memcmp(&out[8], pcm_b, sizeof(pcm_b)) == 0, "block B PCM preserved");
    return 0;
}

static int test_deframer_resyncs_after_corrupted_payload(void)
{
    uint8_t stream[512] = { 0 };
    size_t pos = 0u;
    const int16_t pcm[] = { 10, -10, 20, -20 };

    EXPECT_TRUE(p4_audio_link_init() == 0, "init ok");

    size_t len = build_block(&stream[pos], sizeof(stream) - pos, 1u, 48000u, pcm, 2u, UINT32_MAX);
    EXPECT_TRUE(len > 0u, "block built");
    stream[pos + sizeof(p4_audio_link_block_header_t) + 1u] ^= 0xFFu; /* corrupt payload */
    pos += len;
    len = build_block(&stream[pos], sizeof(stream) - pos, 2u, 48000u, pcm, 2u, UINT32_MAX);
    EXPECT_TRUE(len > 0u, "second block built");
    pos += len;

    p4_audio_link_feed_bytes(stream, pos);

    p4_audio_link_stats_t stats = { 0 };
    p4_audio_link_get_stats(&stats);
    EXPECT_EQ_U32(stats.received_blocks, 1u, "only intact block received");
    EXPECT_EQ_U32(stats.crc_errors, 1u, "corrupted payload counted as crc error");
    EXPECT_EQ_U32(stats.ring_frames, 2u, "intact block frames in ring");
    return 0;
}

int main(void)
{
    if (test_receives_valid_blocks_and_reads_fifo_frames() != 0) return 1;
    if (test_sequence_gap_and_crc_error_are_counted() != 0) return 1;
    if (test_corrupted_header_is_rejected_before_sequence_tracking() != 0) return 1;
    if (test_underrun_returns_silence() != 0) return 1;
    if (test_overrun_drops_oldest_frames() != 0) return 1;
    if (test_deframer_reassembles_chunked_stream_with_filler() != 0) return 1;
    if (test_deframer_resyncs_after_corrupted_payload() != 0) return 1;

    puts("p4_audio_link: PASS");
    return 0;
}
