#include "flx4_usb_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static uint8_t *read_fixture(size_t *out_len)
{
    const char *path = "fixtures/flx4_config_descriptor.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        path = "tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin";
        f = fopen(path, "rb");
    }
    if (!f) {
        fprintf(stderr, "FAIL: fixture opens\n");
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "FAIL: fixture seek end\n");
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len <= 9) {
        fprintf(stderr, "FAIL: fixture has descriptor bytes\n");
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "FAIL: fixture rewind\n");
        fclose(f);
        return NULL;
    }

    uint8_t *data = malloc((size_t)len);
    if (!data) {
        fprintf(stderr, "FAIL: fixture allocation\n");
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "FAIL: fixture read\n");
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return data;
}

static int test_configure_selects_flx4_playback_format_without_streaming(void)
{
    size_t len = 0;
    uint8_t *descriptor = read_fixture(&len);

    EXPECT_TRUE(flx4_usb_audio_configure(NULL, NULL, descriptor, len) == 0, "configure succeeds");

    flx4_usb_audio_stats_t stats = { 0 };
    flx4_usb_audio_get_stats(&stats);
    EXPECT_TRUE(stats.configured, "stats report configured");
    EXPECT_EQ_U32(stats.format.interface_num, 1u, "selected interface");
    EXPECT_EQ_U32(stats.format.alternate_setting, 2u, "selected 16-bit alt setting");
    EXPECT_EQ_U32(stats.format.endpoint_addr, 0x01u, "selected OUT endpoint");
    EXPECT_EQ_U32(stats.format.channels, 4u, "selected FLX4 playback channels");
    EXPECT_EQ_U32(stats.format.bits_per_sample, 16u, "selected bits");
    EXPECT_EQ_U32(stats.submitted_packets, 0u, "configure does not start streaming");
    EXPECT_EQ_U32(stats.completed_packets, 0u, "configure has no completions");
    EXPECT_EQ_U32(stats.actual_bytes, 0u, "configure has no bytes");

    free(descriptor);
    return 0;
}

static int test_tone_packet_uses_headphone_candidate_channels_by_default(void)
{
    size_t len = 0;
    uint8_t *descriptor = read_fixture(&len);
    uint8_t packet[384] = { 0 };

    EXPECT_TRUE(flx4_usb_audio_configure(NULL, NULL, descriptor, len) == 0, "configure succeeds");
    EXPECT_TRUE(flx4_usb_audio_start_tone(1000u) == 0, "tone start succeeds");
    size_t bytes = flx4_usb_audio_fill_next_tone_packet(packet, sizeof(packet));
    EXPECT_EQ_U32(bytes, 384u, "48k 4ch 16-bit packet size");

    bool ch12_nonzero = false;
    bool ch34_nonzero = false;
    for (size_t frame = 0; frame < 48u; ++frame) {
        int16_t ch1, ch2, ch3, ch4;
        memcpy(&ch1, &packet[(frame * 4u + 0u) * sizeof(int16_t)], sizeof(ch1));
        memcpy(&ch2, &packet[(frame * 4u + 1u) * sizeof(int16_t)], sizeof(ch2));
        memcpy(&ch3, &packet[(frame * 4u + 2u) * sizeof(int16_t)], sizeof(ch3));
        memcpy(&ch4, &packet[(frame * 4u + 3u) * sizeof(int16_t)], sizeof(ch4));
        ch12_nonzero = ch12_nonzero || ch1 != 0 || ch2 != 0;
        ch34_nonzero = ch34_nonzero || ch3 != 0 || ch4 != 0;
    }
    EXPECT_TRUE(!ch12_nonzero, "channels 1/2 are silent by default");
    EXPECT_TRUE(ch34_nonzero, "channels 3/4 carry tone by default");

    flx4_usb_audio_stats_t stats = { 0 };
    flx4_usb_audio_get_stats(&stats);
    EXPECT_EQ_U32(stats.submitted_packets, 1u, "tone fill counts submitted packet");
    EXPECT_EQ_U32(stats.actual_bytes, 384u, "tone fill counts bytes");

    free(descriptor);
    return 0;
}

static int test_start_tone_requires_configuration(void)
{
    flx4_usb_audio_stop();
    EXPECT_TRUE(flx4_usb_audio_start_tone(1000u) != 0, "tone start requires configure");
    return 0;
}

extern uint32_t flx4_usb_audio_pc_stream_sample_rate(void);
extern int flx4_usb_audio_pc_apply_link_rate(uint32_t link_sample_rate);

static int test_ring_stream_tracks_p4_link_rate_after_start(void)
{
    size_t len = 0;
    uint8_t *descriptor = read_fixture(&len);

    EXPECT_TRUE(flx4_usb_audio_configure(NULL, NULL, descriptor, len) == 0, "configure succeeds");
    EXPECT_TRUE(flx4_usb_audio_start_ring() == 0, "ring start succeeds");
    EXPECT_EQ_U32(flx4_usb_audio_pc_stream_sample_rate(), 48000u, "ring starts at preferred rate");

    EXPECT_TRUE(flx4_usb_audio_pc_apply_link_rate(44100u) == 0, "supported link rate accepted");
    EXPECT_EQ_U32(flx4_usb_audio_pc_stream_sample_rate(), 44100u, "ring stream follows P4 link rate");

    EXPECT_TRUE(flx4_usb_audio_pc_apply_link_rate(32000u) == 0, "unsupported link rate is a no-op");
    EXPECT_EQ_U32(flx4_usb_audio_pc_stream_sample_rate(), 44100u, "unsupported link rate keeps current rate");

    free(descriptor);
    return 0;
}

int main(void)
{
    if (test_configure_selects_flx4_playback_format_without_streaming() != 0) return 1;
    if (test_tone_packet_uses_headphone_candidate_channels_by_default() != 0) return 1;
    if (test_start_tone_requires_configuration() != 0) return 1;
    if (test_ring_stream_tracks_p4_link_rate_after_start() != 0) return 1;

    puts("flx4_usb_audio_runtime: PASS");
    return 0;
}
