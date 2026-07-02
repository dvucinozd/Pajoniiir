#include "monitor_pcm_link.h"

#include <stdint.h>
#include <stdio.h>

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
    EXPECT_EQ(stats.dropped_blocks, 1u, "drop counted");
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

int main(void)
{
    if (test_disabled_link_drops_without_failure() != 0) {
        return 1;
    }
    if (test_invalid_format_is_rejected() != 0) {
        return 1;
    }

    puts("monitor_pcm_link: PASS");
    return 0;
}
