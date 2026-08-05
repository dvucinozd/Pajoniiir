#include "controller_audio_ring.h"
#include "flx4_uac_descriptors.h"
#include "flx4_uac_packetizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static void test_packetizer(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 44100u, 4u, 2u);
    uint32_t sum = 0u;
    unsigned count44 = 0u;
    unsigned count45 = 0u;
    for (unsigned i = 0u; i < 1000u; ++i) {
        const uint16_t frames = flx4_uac_packetizer_next_frames(&p);
        CHECK(frames == 44u || frames == 45u);
        sum += frames;
        count44 += frames == 44u;
        count45 += frames == 45u;
    }
    CHECK(sum == 44100u);
    CHECK(count44 == 900u);
    CHECK(count45 == 100u);

    flx4_uac_packetizer_init(&p, 48000u, 2u, 2u);
    for (unsigned i = 0u; i < 1000u; ++i) {
        CHECK(flx4_uac_packetizer_next_frames(&p) == 48u);
    }
    flx4_uac_packetizer_init(&p, 48000u, 4u, 2u);
    CHECK(flx4_uac_packetizer_next_bytes(&p) == 384u);
    CHECK(flx4_uac_packetizer_next_frames(NULL) == 0u);
}

static void test_descriptors(void)
{
    const uint8_t descriptor[] = {
        9, 2, 36, 0, 1, 1, 0, 0x80, 50,
        9, 4, 2, 1, 1, 1, 2, 0, 0,
        11, 0x24, 2, 1, 4, 2, 16, 1, 0x80, 0xBB, 0x00,
        7, 5, 1, 1, 0x80, 0x01, 1,
    };
    flx4_uac_descriptor_result_t result;
    CHECK(flx4_uac_parse_playback_formats(descriptor, sizeof(descriptor), &result));
    CHECK(result.format_count == 1u);
    CHECK(result.formats[0].interface_num == 2u);
    CHECK(result.formats[0].alternate_setting == 1u);
    CHECK(result.formats[0].endpoint_addr == 1u);
    CHECK(result.formats[0].channels == 4u);
    CHECK(result.formats[0].sample_rates[0] == 48000u);
    flx4_uac_playback_format_t preferred;
    CHECK(flx4_uac_select_preferred_format(&result, &preferred));
    CHECK(preferred.max_packet_size == 384u);

    uint8_t malformed[sizeof(descriptor)];
    memcpy(malformed, descriptor, sizeof(descriptor));
    malformed[9] = 0u;
    CHECK(!flx4_uac_parse_playback_formats(malformed, sizeof(malformed), &result));
    CHECK(!flx4_uac_parse_playback_formats(NULL, 0u, &result));
    CHECK(!flx4_uac_select_preferred_format(NULL, &preferred));

    memcpy(malformed, descriptor, sizeof(descriptor));
    malformed[9 + 9 + 6] = 24u;
    CHECK(flx4_uac_parse_playback_formats(malformed, sizeof(malformed), &result));
    CHECK(!flx4_uac_select_preferred_format(&result, &preferred));
}

static void test_ring(void)
{
    int16_t storage[8] = {0};
    controller_audio_ring_t ring;
    CHECK(!controller_audio_ring_init(NULL, storage, 4u, 2u, 48000u));
    CHECK(controller_audio_ring_init(&ring, storage, 4u, 2u, 48000u));
    CHECK(controller_audio_ring_free(&ring) == 4u);
    CHECK(ring.generation == 1u);

    const int16_t first[] = {1,2, 3,4, 5,6};
    CHECK(controller_audio_ring_write(&ring, first, 3u) == 3u);
    CHECK(controller_audio_ring_queued(&ring) == 3u);
    int16_t out[12];
    memset(out, 0x7F, sizeof(out));
    CHECK(controller_audio_ring_read(&ring, out, 2u, false) == 2u);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4);

    const int16_t second[] = {7,8, 9,10, 11,12, 13,14};
    CHECK(controller_audio_ring_write(&ring, second, 4u) == 3u);
    CHECK(ring.overrun_frames == 1u);
    CHECK(controller_audio_ring_queued(&ring) == 4u);
    memset(out, 0, sizeof(out));
    CHECK(controller_audio_ring_read(&ring, out, 4u, false) == 4u);
    const int16_t expected[] = {5,6, 7,8, 9,10, 11,12};
    CHECK(memcmp(out, expected, sizeof(expected)) == 0);

    memset(out, 0x7F, sizeof(out));
    CHECK(controller_audio_ring_read(&ring, out, 3u, true) == 0u);
    CHECK(ring.underrun_frames == 3u);
    for (unsigned i = 0u; i < 6u; ++i) {
        CHECK(out[i] == 0);
    }

    const uint32_t generation = ring.generation;
    CHECK(controller_audio_ring_write(&ring, first, 2u) == 2u);
    controller_audio_ring_reset(&ring, 44100u);
    CHECK(ring.generation == generation + 1u);
    CHECK(ring.sample_rate == 44100u);
    CHECK(controller_audio_ring_queued(&ring) == 0u);
    CHECK(ring.written_frames == 8u);
}

int main(void)
{
    test_packetizer();
    test_descriptors();
    test_ring();
    printf("P4 USB Audio pure core: %u checks passed\n", checks);
    return 0;
}
