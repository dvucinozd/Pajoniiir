#include "flx4_uac_descriptors.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static bool has_rate(const flx4_uac_playback_format_t *fmt, uint32_t rate)
{
    for (uint8_t i = 0; i < fmt->sample_rate_count; ++i) {
        if (fmt->sample_rates[i] == rate) {
            return true;
        }
    }
    return false;
}

static int test_parse_real_flx4_descriptor(void)
{
    size_t len = 0;
    uint8_t *data = read_fixture(&len);
    EXPECT_TRUE(data != NULL, "fixture loaded");

    flx4_uac_descriptor_result_t result = { 0 };
    EXPECT_TRUE(flx4_uac_parse_playback_formats(data, len, &result), "parse returns true");
    EXPECT_EQ_U32(result.format_count, 2u, "finds the two FLX4 playback alt-settings");

    const flx4_uac_playback_format_t *alt1 = &result.formats[0];
    EXPECT_EQ_U32(alt1->interface_num, 1u, "alt1 interface");
    EXPECT_EQ_U32(alt1->alternate_setting, 1u, "alt1 setting");
    EXPECT_EQ_U32(alt1->endpoint_addr, 0x01u, "alt1 playback endpoint");
    EXPECT_EQ_U32(alt1->max_packet_size, 576u, "alt1 max packet");
    EXPECT_EQ_U32(alt1->channels, 4u, "alt1 channels");
    EXPECT_EQ_U32(alt1->bytes_per_sample, 3u, "alt1 bytes per sample");
    EXPECT_EQ_U32(alt1->bits_per_sample, 24u, "alt1 bits");
    EXPECT_TRUE(has_rate(alt1, 48000u), "alt1 has 48 kHz");
    EXPECT_TRUE(has_rate(alt1, 44100u), "alt1 has 44.1 kHz");

    const flx4_uac_playback_format_t *alt2 = &result.formats[1];
    EXPECT_EQ_U32(alt2->interface_num, 1u, "alt2 interface");
    EXPECT_EQ_U32(alt2->alternate_setting, 2u, "alt2 setting");
    EXPECT_EQ_U32(alt2->endpoint_addr, 0x01u, "alt2 playback endpoint");
    EXPECT_EQ_U32(alt2->max_packet_size, 384u, "alt2 max packet");
    EXPECT_EQ_U32(alt2->channels, 4u, "alt2 channels");
    EXPECT_EQ_U32(alt2->bytes_per_sample, 2u, "alt2 bytes per sample");
    EXPECT_EQ_U32(alt2->bits_per_sample, 16u, "alt2 bits");
    EXPECT_TRUE(has_rate(alt2, 48000u), "alt2 has 48 kHz");
    EXPECT_TRUE(has_rate(alt2, 44100u), "alt2 has 44.1 kHz");

    free(data);
    return 0;
}

static int test_select_preferred_format(void)
{
    flx4_uac_descriptor_result_t result = {
        .formats = {
            { .interface_num = 1, .alternate_setting = 1, .endpoint_addr = 0x01, .max_packet_size = 576, .channels = 4, .bits_per_sample = 24, .bytes_per_sample = 3, .sample_rates = { 48000, 44100 }, .sample_rate_count = 2 },
            { .interface_num = 1, .alternate_setting = 2, .endpoint_addr = 0x01, .max_packet_size = 384, .channels = 4, .bits_per_sample = 16, .bytes_per_sample = 2, .sample_rates = { 48000, 44100 }, .sample_rate_count = 2 },
            { .interface_num = 2, .alternate_setting = 1, .endpoint_addr = 0x81, .max_packet_size = 192, .channels = 2, .bits_per_sample = 16, .bytes_per_sample = 2, .sample_rates = { 48000 }, .sample_rate_count = 1 },
        },
        .format_count = 3,
    };

    flx4_uac_playback_format_t selected = { 0 };
    EXPECT_TRUE(flx4_uac_select_preferred_format(&result, &selected), "select succeeds");
    EXPECT_EQ_U32(selected.interface_num, 1u, "selects playback interface");
    EXPECT_EQ_U32(selected.alternate_setting, 2u, "selects 4ch 16-bit alt-setting");
    EXPECT_EQ_U32(selected.endpoint_addr, 0x01u, "selected endpoint is OUT");
    EXPECT_EQ_U32(selected.bits_per_sample, 16u, "prefers 16-bit");
    EXPECT_EQ_U32(selected.channels, 4u, "keeps FLX4 playback channel width");
    EXPECT_TRUE(has_rate(&selected, 48000u), "selected format has 48 kHz");
    return 0;
}

int main(void)
{
    if (test_parse_real_flx4_descriptor() != 0) {
        return 1;
    }
    if (test_select_preferred_format() != 0) {
        return 1;
    }
    puts("flx4_uac_descriptors: PASS");
    return 0;
}
