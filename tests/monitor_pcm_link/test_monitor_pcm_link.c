#include "monitor_pcm_link.h"

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

#define EXPECT_EQ(actual, expected, msg) \
    do { \
        unsigned actual_ = (unsigned)(actual); \
        unsigned expected_ = (unsigned)(expected); \
        if (actual_ != expected_) { \
            fprintf(stderr, "FAIL: %s got=%u expected=%u\n", msg, actual_, expected_); \
            return 1; \
        } \
    } while (0)

static int test_disabled_link_drops_without_failure(void)
{
    int16_t block[256 * 2] = { 0 };

    EXPECT_TRUE(monitor_pcm_link_init() == 0, "init ok");
    EXPECT_TRUE(monitor_pcm_link_set_format(48000u, 2u, 16u) == 0, "format ok");
    EXPECT_TRUE(!monitor_pcm_link_write_nonblocking(block, 256u), "disabled link reports not submitted");

    monitor_pcm_link_stats_t stats = { 0 };
    monitor_pcm_link_get_stats(&stats);
    EXPECT_EQ(stats.sample_rate, 48000u, "sample rate kept");
    EXPECT_EQ(stats.channels, 2u, "channel count kept");
    EXPECT_EQ(stats.bits_per_sample, 16u, "bit depth kept");
    /* A write while the link is disabled is a no-op, not a drop: the output task
     * calls this every block regardless of the enable state, so counting drops
     * here would inflate the diagnostics into the millions when the feature is
     * off. */
    EXPECT_EQ(stats.dropped_blocks, 0u, "disabled write not counted as a drop");
    EXPECT_EQ(stats.submitted_blocks, 0u, "no submitted blocks");
    EXPECT_EQ(stats.submitted_frames, 0u, "no submitted frames");
    return 0;
}

static int test_invalid_format_is_rejected(void)
{
    EXPECT_TRUE(monitor_pcm_link_init() == 0, "init ok");
    EXPECT_TRUE(monitor_pcm_link_set_format(0u, 2u, 16u) != 0, "zero sample rate rejected");
    EXPECT_TRUE(monitor_pcm_link_set_format(48000u, 0u, 16u) != 0, "zero channels rejected");
    EXPECT_TRUE(monitor_pcm_link_set_format(48000u, 2u, 0u) != 0, "zero bits rejected");
    return 0;
}

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

static uint32_t test_block_crc32(const monitor_pcm_link_block_header_t *header,
                                 const int16_t *pcm,
                                 size_t payload_bytes)
{
    monitor_pcm_link_block_header_t protected_header = *header;
    protected_header.block_crc32 = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    crc = test_crc32_update(crc, (const uint8_t *)&protected_header, sizeof(protected_header));
    crc = test_crc32_update(crc, (const uint8_t *)pcm, payload_bytes);
    return ~crc;
}

static int test_enabled_link_serializes_pcm_blocks(void)
{
    const int16_t pcm[] = {
        100, -100,
        200, -200,
        300, -300,
    };
    uint8_t block[128] = { 0 };

    EXPECT_TRUE(monitor_pcm_link_init() == 0, "init ok");
    EXPECT_TRUE(monitor_pcm_link_set_format(48000u, 2u, 16u) == 0, "format ok");
    monitor_pcm_link_set_enabled(true);
    EXPECT_TRUE(monitor_pcm_link_write_nonblocking(pcm, 3u), "enabled link accepts block");

    monitor_pcm_link_stats_t stats = { 0 };
    monitor_pcm_link_get_stats(&stats);
    EXPECT_EQ(stats.submitted_blocks, 1u, "submitted block counted");
    EXPECT_EQ(stats.submitted_frames, 3u, "submitted frames counted");
    EXPECT_EQ(stats.dropped_blocks, 0u, "no drop for first queued block");

    size_t block_len = monitor_pcm_link_read_block_for_transport(block, sizeof(block));
    EXPECT_EQ(block_len, sizeof(monitor_pcm_link_block_header_t) + sizeof(pcm), "serialized block length");

    monitor_pcm_link_block_header_t hdr = { 0 };
    memcpy(&hdr, block, sizeof(hdr));
    EXPECT_EQ(hdr.magic, MONITOR_PCM_LINK_BLOCK_MAGIC, "serialized magic");
    EXPECT_EQ(hdr.header_bytes, sizeof(monitor_pcm_link_block_header_t), "serialized header size");
    EXPECT_EQ(hdr.frames, 3u, "serialized frame count");
    EXPECT_EQ(hdr.sample_rate, 48000u, "serialized sample rate");
    EXPECT_EQ(hdr.sequence, 1u, "first sequence number");
    EXPECT_EQ(hdr.block_crc32, test_block_crc32(&hdr, pcm, sizeof(pcm)), "serialized block crc");
    EXPECT_TRUE(memcmp(block + sizeof(hdr), pcm, sizeof(pcm)) == 0, "serialized PCM payload");
    return 0;
}

static int test_enabled_link_drops_when_queue_is_full(void)
{
    int16_t pcm[MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK * 2] = { 0 };

    EXPECT_TRUE(monitor_pcm_link_init() == 0, "init ok");
    EXPECT_TRUE(monitor_pcm_link_set_format(48000u, 2u, 16u) == 0, "format ok");
    monitor_pcm_link_set_enabled(true);

    for (uint32_t i = 0; i < MONITOR_PCM_LINK_QUEUE_DEPTH; ++i) {
        EXPECT_TRUE(monitor_pcm_link_write_nonblocking(pcm, 1u), "queue slot accepts block");
    }
    EXPECT_TRUE(!monitor_pcm_link_write_nonblocking(pcm, 1u), "full queue drops without blocking");

    monitor_pcm_link_stats_t stats = { 0 };
    monitor_pcm_link_get_stats(&stats);
    EXPECT_EQ(stats.submitted_blocks, MONITOR_PCM_LINK_QUEUE_DEPTH, "submitted count stops at queue capacity");
    EXPECT_EQ(stats.dropped_blocks, 1u, "full queue drop counted");
    return 0;
}

int main(void)
{
    if (test_disabled_link_drops_without_failure() != 0) {
        return 1;
    }
    if (test_invalid_format_is_rejected() != 0) {
        return 1;
    }
    if (test_enabled_link_serializes_pcm_blocks() != 0) {
        return 1;
    }
    if (test_enabled_link_drops_when_queue_is_full() != 0) {
        return 1;
    }

    puts("monitor_pcm_link: PASS");
    return 0;
}
