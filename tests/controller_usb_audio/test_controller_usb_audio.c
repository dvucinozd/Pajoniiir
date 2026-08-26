#include "controller_audio_ring.h"
#include "controller_audio_resampler.h"
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

static void fill_resampler_ramp(int16_t *frames,
                                size_t first_frame,
                                size_t frame_count)
{
    for (size_t frame = 0u; frame < frame_count; ++frame) {
        for (size_t channel = 0u; channel < 4u; ++channel) {
            frames[frame * 4u + channel] = (int16_t)(
                ((first_frame + frame) * 31u + channel * 7u) & 0x7fffu);
        }
    }
}

static void test_resampler(void)
{
    enum { FRAMES = 480, FIRST = 127, CHANNELS = 4, OUTPUT_CAPACITY = 442 };
    int16_t input[FRAMES * CHANNELS];
    int16_t whole[OUTPUT_CAPACITY * CHANNELS];
    int16_t split[OUTPUT_CAPACITY * CHANNELS];
    fill_resampler_ramp(input, 0u, FRAMES);

    controller_audio_resampler_t pass;
    CHECK(controller_audio_resampler_init(&pass, 44100u, 44100u, CHANNELS));
    CHECK(controller_audio_resampler_process(
              &pass, input, FIRST, split, FIRST + 1u) == FIRST);
    CHECK(memcmp(input, split, FIRST * CHANNELS * sizeof(*input)) == 0);

    controller_audio_resampler_t whole_resampler;
    CHECK(controller_audio_resampler_init(
        &whole_resampler, 48000u, 44100u, CHANNELS));
    const size_t whole_frames = controller_audio_resampler_process(
        &whole_resampler, input, FRAMES, whole, OUTPUT_CAPACITY);
    CHECK(whole_frames == 441u);

    controller_audio_resampler_t split_resampler;
    CHECK(controller_audio_resampler_init(
        &split_resampler, 48000u, 44100u, CHANNELS));
    const size_t split_first = controller_audio_resampler_process(
        &split_resampler, input, FIRST, split, OUTPUT_CAPACITY);
    const size_t split_second = controller_audio_resampler_process(
        &split_resampler, &input[FIRST * CHANNELS], FRAMES - FIRST,
        &split[split_first * CHANNELS], OUTPUT_CAPACITY - split_first);
    CHECK(split_first + split_second == whole_frames);
    CHECK(memcmp(whole, split,
                 whole_frames * CHANNELS * sizeof(*whole)) == 0);

    controller_audio_resampler_t second;
    CHECK(controller_audio_resampler_init(&second, 48000u, 44100u, CHANNELS));
    enum { INPUT_CHUNK = 128, CHUNK_OUTPUT_CAPACITY = 129 };
    int16_t chunk_input[INPUT_CHUNK * CHANNELS];
    int16_t chunk_output[CHUNK_OUTPUT_CAPACITY * CHANNELS];
    size_t source_frame = 0u;
    size_t total_output = 0u;
    while (source_frame < 48000u) {
        size_t chunk = 48000u - source_frame;
        if (chunk > INPUT_CHUNK) {
            chunk = INPUT_CHUNK;
        }
        fill_resampler_ramp(chunk_input, source_frame, chunk);
        total_output += controller_audio_resampler_process(
            &second, chunk_input, chunk, chunk_output, CHUNK_OUTPUT_CAPACITY);
        source_frame += chunk;
    }
    CHECK(total_output == 44100u);

    CHECK(!controller_audio_resampler_init(NULL, 48000u, 44100u, 4u));
    CHECK(!controller_audio_resampler_init(&second, 0u, 44100u, 4u));
    CHECK(!controller_audio_resampler_init(&second, 48000u, 0u, 4u));
    CHECK(!controller_audio_resampler_init(&second, 48000u, 44100u, 0u));
    CHECK(!controller_audio_resampler_init(&second, 48000u, 44100u, 9u));
    CHECK(controller_audio_resampler_output_bound(0u, 44100u, 4u) == 0u);
    CHECK(controller_audio_resampler_output_bound(48000u, 0u, 4u) == 0u);
    CHECK(controller_audio_resampler_output_bound(48000u, 44100u, 0u) == 0u);

    CHECK(controller_audio_resampler_init(&second, 48000u, 44100u, 4u));
    const size_t bound =
        controller_audio_resampler_output_bound(48000u, 44100u, 4u);
    CHECK(bound > 0u);
    CHECK(controller_audio_resampler_process(
              &second, input, 4u, whole, bound - 1u) == 0u);
    CHECK(!second.has_previous);
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
    CHECK(ring.high_water_frames == 3u);
    int16_t out[12];
    memset(out, 0x7F, sizeof(out));
    CHECK(controller_audio_ring_read(&ring, out, 2u, false) == 2u);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4);

    const int16_t second[] = {7,8, 9,10, 11,12, 13,14};
    CHECK(controller_audio_ring_write(&ring, second, 4u) == 3u);
    CHECK(ring.overrun_frames == 1u);
    CHECK(controller_audio_ring_queued(&ring) == 4u);
    CHECK(ring.high_water_frames == 4u);
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
    CHECK(ring.high_water_frames == 0u);
    CHECK(ring.overrun_frames == 0u);
    CHECK(ring.underrun_frames == 0u);
}

static void test_clocked_ring(void)
{
    int16_t storage[16u * 4u] = {0};
    const int16_t input[4u * 4u] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
    };
    controller_audio_ring_t ring;
    CHECK(controller_audio_ring_init(&ring, storage, 16u, 4u, 44100u));
    CHECK(controller_audio_ring_write_clocked(&ring, input, 4u) == 5u);
    CHECK(controller_audio_ring_write_clocked(&ring, input, 4u) == 5u);
    CHECK(controller_audio_ring_write_clocked(&ring, input, 4u) == 3u);
    CHECK(controller_audio_ring_queued(&ring) == 13u);
    CHECK(ring.clock_duplicated_frames == 2u);
    CHECK(ring.clock_trimmed_frames == 1u);
    CHECK(ring.overrun_frames == 0u);
    CHECK(ring.high_water_frames == 13u);
    CHECK(controller_audio_ring_write_clocked(NULL, input, 4u) == 0u);
    CHECK(controller_audio_ring_write_clocked(&ring, NULL, 4u) == 0u);
    CHECK(controller_audio_ring_write_clocked(&ring, input, 0u) == 0u);
}

int main(void)
{
    test_packetizer();
    test_descriptors();
    test_resampler();
    test_ring();
    test_clocked_ring();
    printf("P4 USB Audio pure core: %u checks passed\n", checks);
    return 0;
}
