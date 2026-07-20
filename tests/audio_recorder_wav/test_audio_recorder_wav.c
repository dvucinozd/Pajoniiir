/*
 * Host tests for the pure P4 master-recorder WAV helpers.
 *
 * Covers header encoding, in-place size patching, `.part` segment naming and
 * crash-recovery data-byte truncation. No file, allocation or audio work.
 */
#include "audio_recorder_wav.h"

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

static uint32_t load_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t load_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void test_build_header(void)
{
    printf("== build_header ==\n");
    uint8_t h[AUDIO_RECORDER_WAV_HEADER_BYTES];

    /* 48 kHz, 8 payload bytes (two stereo frames). */
    audio_recorder_wav_build_header(h, 48000u, 8u);
    CHECK(memcmp(h + 0, "RIFF", 4) == 0);
    CHECK(load_u32le(h + 4) == 36u + 8u);
    CHECK(memcmp(h + 8, "WAVE", 4) == 0);
    CHECK(memcmp(h + 12, "fmt ", 4) == 0);
    CHECK(load_u32le(h + 16) == 16u);
    CHECK(load_u16le(h + 20) == 1u);        /* PCM */
    CHECK(load_u16le(h + 22) == 2u);        /* stereo */
    CHECK(load_u32le(h + 24) == 48000u);
    CHECK(load_u32le(h + 28) == 48000u * 2u * 2u);  /* byte rate */
    CHECK(load_u16le(h + 32) == 4u);        /* block align */
    CHECK(load_u16le(h + 34) == 16u);       /* bits */
    CHECK(memcmp(h + 36, "data", 4) == 0);
    CHECK(load_u32le(h + 40) == 8u);

    /* 44.1 kHz byte rate must reflect the requested rate. */
    audio_recorder_wav_build_header(h, 44100u, 0u);
    CHECK(load_u32le(h + 24) == 44100u);
    CHECK(load_u32le(h + 28) == 44100u * 4u);
    CHECK(load_u32le(h + 4) == 36u);        /* empty payload */
    CHECK(load_u32le(h + 40) == 0u);
}

static void test_patch_sizes(void)
{
    printf("== patch_sizes ==\n");
    uint8_t h[AUDIO_RECORDER_WAV_HEADER_BYTES];
    audio_recorder_wav_build_header(h, 48000u, 0u);

    audio_recorder_wav_patch_sizes(h, 1000u);
    CHECK(load_u32le(h + 4) == 36u + 1000u);
    CHECK(load_u32le(h + 40) == 1000u);
    /* Format fields must be untouched by a size patch. */
    CHECK(load_u32le(h + 24) == 48000u);
    CHECK(memcmp(h + 36, "data", 4) == 0);
}

static void test_format_segment(void)
{
    printf("== format_segment ==\n");
    char name[64];

    int n = audio_recorder_wav_format_segment(name, sizeof(name), 42u, 1u, 3u, 48000u);
    CHECK(n > 0);
    CHECK(strcmp(name, "REC_B42_1_3_48000Hz.wav.part") == 0);
    CHECK((size_t)n == strlen(name));

    /* A buffer that is too small must fail cleanly and empty the output. */
    char tiny[8];
    int rc = audio_recorder_wav_format_segment(tiny, sizeof(tiny), 42u, 1u, 3u, 48000u);
    CHECK(rc == 0);
    CHECK(tiny[0] == '\0');
}

static void test_recover_data_bytes(void)
{
    printf("== recover_data_bytes ==\n");
    uint32_t data = 0xDEADBEEFu;

    /* Header-only or partial-header files carry no complete frame. */
    CHECK(!audio_recorder_wav_recover_data_bytes(0u, &data));
    CHECK(!audio_recorder_wav_recover_data_bytes(44u, &data));
    CHECK(!audio_recorder_wav_recover_data_bytes(47u, &data));   /* 44 + 3 */

    /* Exactly one frame, and a partial trailing frame truncated away. */
    data = 0;
    CHECK(audio_recorder_wav_recover_data_bytes(48u, &data) && data == 4u);   /* 44 + 4 */
    data = 0;
    CHECK(audio_recorder_wav_recover_data_bytes(51u, &data) && data == 4u);   /* 44 + 7 -> 4 */
    data = 0;
    CHECK(audio_recorder_wav_recover_data_bytes(52u, &data) && data == 8u);   /* 44 + 8 */

    /* A realistic 1 MiB payload rounds to whole frames. */
    data = 0;
    CHECK(audio_recorder_wav_recover_data_bytes(44u + 1048576u, &data) && data == 1048576u);

    /* An implausibly huge file (beyond a 32-bit data size) is rejected. */
    CHECK(!audio_recorder_wav_recover_data_bytes(44ull + 0x100000000ull, &data));
}

int main(void)
{
    printf("=== audio_recorder_wav tests ===\n");
    test_build_header();
    test_patch_sizes();
    test_format_segment();
    test_recover_data_bytes();

    if (s_failures == 0) {
        printf("audio_recorder_wav tests passed\n");
        return 0;
    }
    printf("audio_recorder_wav tests FAILED (%d)\n", s_failures);
    return 1;
}
