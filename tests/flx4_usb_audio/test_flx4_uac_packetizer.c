#include "flx4_uac_packetizer.h"

#include <stdint.h>
#include <stdio.h>

#define EXPECT_EQ(actual, expected, msg) \
    do { \
        unsigned actual_ = (unsigned)(actual); \
        unsigned expected_ = (unsigned)(expected); \
        if (actual_ != expected_) { \
            fprintf(stderr, "FAIL: %s got=%u expected=%u\n", msg, actual_, expected_); \
            return 1; \
        } \
    } while (0)

static int test_48k_4ch_16_is_constant(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 48000u, 4u, 2u);

    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(flx4_uac_packetizer_next_frames(&p), 48u, "48k frames per ms");
    }

    return 0;
}

static int test_44k1_accumulates_44100_frames_per_second(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 44100u, 4u, 2u);

    uint32_t total = 0;
    uint32_t packets_44 = 0;
    uint32_t packets_45 = 0;
    for (int i = 0; i < 1000; ++i) {
        uint16_t frames = flx4_uac_packetizer_next_frames(&p);
        total += frames;
        if (frames == 44u) {
            packets_44++;
        } else if (frames == 45u) {
            packets_45++;
        } else {
            fprintf(stderr, "FAIL: unexpected 44.1k packet size got=%u\n", (unsigned)frames);
            return 1;
        }
    }

    EXPECT_EQ(total, 44100u, "44.1k total frames per 1000 USB frames");
    EXPECT_EQ(packets_44, 900u, "44-frame packets");
    EXPECT_EQ(packets_45, 100u, "45-frame packets");
    return 0;
}

static int test_4ch_16_byte_count(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 48000u, 4u, 2u);

    EXPECT_EQ(flx4_uac_packetizer_next_bytes(&p), 384u, "48k 4ch 16-bit bytes per ms");
    return 0;
}

static int test_invalid_packetizer_inputs_are_silent(void)
{
    EXPECT_EQ(flx4_uac_packetizer_next_frames(NULL), 0u, "null frames");
    EXPECT_EQ(flx4_uac_packetizer_next_bytes(NULL), 0u, "null bytes");

    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 0u, 4u, 2u);
    EXPECT_EQ(flx4_uac_packetizer_next_frames(&p), 0u, "zero sample rate");
    EXPECT_EQ(flx4_uac_packetizer_next_bytes(&p), 0u, "zero sample rate bytes");
    return 0;
}

int main(void)
{
    if (test_48k_4ch_16_is_constant() != 0) {
        return 1;
    }
    if (test_44k1_accumulates_44100_frames_per_second() != 0) {
        return 1;
    }
    if (test_4ch_16_byte_count() != 0) {
        return 1;
    }
    if (test_invalid_packetizer_inputs_are_silent() != 0) {
        return 1;
    }

    puts("flx4_uac_packetizer: PASS");
    return 0;
}
