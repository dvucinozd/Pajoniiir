#include "p4_audio_link.h"

#include <string.h>

typedef struct {
    int16_t left;
    int16_t right;
} p4_audio_link_frame_t;

static p4_audio_link_frame_t s_ring[P4_AUDIO_LINK_RING_CAPACITY_FRAMES];
static uint32_t s_read_index;
static uint32_t s_write_index;
static uint32_t s_used_frames;
static uint32_t s_last_sequence;
static bool s_have_sequence;
static p4_audio_link_stats_t s_stats;

static uint32_t p4_audio_link_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void p4_audio_link_push_frame(int16_t left, int16_t right)
{
    if (s_used_frames >= P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {
        s_read_index = (s_read_index + 1u) % P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
        s_used_frames--;
        s_stats.overruns++;
    }

    s_ring[s_write_index].left = left;
    s_ring[s_write_index].right = right;
    s_write_index = (s_write_index + 1u) % P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    s_used_frames++;
}

esp_err_t p4_audio_link_init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.ring_capacity_frames = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    s_read_index = 0u;
    s_write_index = 0u;
    s_used_frames = 0u;
    s_last_sequence = 0u;
    s_have_sequence = false;
    return ESP_OK;
}

bool p4_audio_link_receive_block(const uint8_t *block, size_t block_len)
{
    if (!block || block_len < sizeof(p4_audio_link_block_header_t)) {
        return false;
    }

    p4_audio_link_block_header_t hdr;
    memcpy(&hdr, block, sizeof(hdr));
    if (hdr.magic != P4_AUDIO_LINK_BLOCK_MAGIC ||
        hdr.header_bytes != sizeof(p4_audio_link_block_header_t) ||
        hdr.frames == 0u) {
        return false;
    }

    const size_t payload_bytes = (size_t)hdr.frames * 2u * sizeof(int16_t);
    const size_t expected_len = (size_t)hdr.header_bytes + payload_bytes;
    if (block_len < expected_len) {
        return false;
    }

    const uint8_t *payload = block + hdr.header_bytes;
    uint32_t crc = p4_audio_link_crc32(payload, payload_bytes);
    if (crc != hdr.payload_crc32) {
        s_stats.crc_errors++;
        return false;
    }

    if (s_have_sequence && hdr.sequence != s_last_sequence + 1u) {
        s_stats.sequence_gaps++;
    }
    s_have_sequence = true;
    s_last_sequence = hdr.sequence;

    for (uint16_t i = 0; i < hdr.frames; ++i) {
        int16_t left;
        int16_t right;
        memcpy(&left, payload + ((size_t)i * 2u * sizeof(int16_t)), sizeof(left));
        memcpy(&right, payload + (((size_t)i * 2u + 1u) * sizeof(int16_t)), sizeof(right));
        p4_audio_link_push_frame(left, right);
    }

    s_stats.received_blocks++;
    s_stats.sample_rate = hdr.sample_rate;
    s_stats.ring_frames = s_used_frames;
    return true;
}

size_t p4_audio_link_read_frames(int16_t *dst_interleaved_stereo, size_t frames)
{
    if (!dst_interleaved_stereo || frames == 0u) {
        return 0u;
    }

    size_t read_frames = 0u;
    while (read_frames < frames && s_used_frames > 0u) {
        dst_interleaved_stereo[read_frames * 2u] = s_ring[s_read_index].left;
        dst_interleaved_stereo[read_frames * 2u + 1u] = s_ring[s_read_index].right;
        s_read_index = (s_read_index + 1u) % P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
        s_used_frames--;
        read_frames++;
    }

    if (read_frames < frames) {
        memset(&dst_interleaved_stereo[read_frames * 2u],
               0,
               (frames - read_frames) * 2u * sizeof(int16_t));
        s_stats.underruns++;
    }

    s_stats.ring_frames = s_used_frames;
    return read_frames;
}

void p4_audio_link_get_stats(p4_audio_link_stats_t *out)
{
    if (!out) {
        return;
    }
    s_stats.ring_frames = s_used_frames;
    s_stats.ring_capacity_frames = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    *out = s_stats;
}
