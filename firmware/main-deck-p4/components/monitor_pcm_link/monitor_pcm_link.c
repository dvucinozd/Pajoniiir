#include "monitor_pcm_link.h"

#include <string.h>

typedef struct {
    monitor_pcm_link_block_header_t header;
    int16_t pcm[MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK * 2u];
} monitor_pcm_link_queued_block_t;

static monitor_pcm_link_stats_t s_stats;
static bool s_enabled;
static monitor_pcm_link_queued_block_t s_queue[MONITOR_PCM_LINK_QUEUE_DEPTH];
static uint32_t s_read_index;
static uint32_t s_write_index;
static uint32_t s_queued_blocks;
static uint32_t s_next_sequence;

static uint32_t monitor_pcm_link_crc32(const uint8_t *data, size_t len)
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

esp_err_t monitor_pcm_link_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_queue, 0, sizeof(s_queue));
    s_enabled = false;
    s_read_index = 0u;
    s_write_index = 0u;
    s_queued_blocks = 0u;
    s_next_sequence = 1u;
    return ESP_OK;
}

esp_err_t monitor_pcm_link_set_format(uint32_t sample_rate,
                                      uint8_t channels,
                                      uint8_t bits_per_sample)
{
    if (sample_rate == 0u || channels == 0u || bits_per_sample == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    s_stats.sample_rate = sample_rate;
    s_stats.channels = channels;
    s_stats.bits_per_sample = bits_per_sample;
    return ESP_OK;
}

void monitor_pcm_link_set_enabled(bool enabled)
{
    s_enabled = enabled;
}

bool monitor_pcm_link_write_nonblocking(const int16_t *interleaved_stereo, size_t frames)
{
    if (!interleaved_stereo || frames == 0u || frames > MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK || !s_enabled) {
        s_stats.dropped_blocks++;
        return false;
    }
    if (s_stats.sample_rate == 0u || s_stats.channels != 2u || s_stats.bits_per_sample != 16u) {
        s_stats.dropped_blocks++;
        return false;
    }
    if (s_queued_blocks >= MONITOR_PCM_LINK_QUEUE_DEPTH) {
        s_stats.dropped_blocks++;
        return false;
    }

    monitor_pcm_link_queued_block_t *slot = &s_queue[s_write_index];
    size_t payload_bytes = frames * 2u * sizeof(int16_t);
    memcpy(slot->pcm, interleaved_stereo, payload_bytes);
    slot->header.magic = MONITOR_PCM_LINK_BLOCK_MAGIC;
    slot->header.header_bytes = (uint16_t)sizeof(monitor_pcm_link_block_header_t);
    slot->header.frames = (uint16_t)frames;
    slot->header.sample_rate = s_stats.sample_rate;
    slot->header.sequence = s_next_sequence++;
    slot->header.payload_crc32 = monitor_pcm_link_crc32((const uint8_t *)slot->pcm, payload_bytes);

    s_write_index = (s_write_index + 1u) % MONITOR_PCM_LINK_QUEUE_DEPTH;
    s_queued_blocks++;
    s_stats.submitted_blocks++;
    s_stats.submitted_frames += (uint32_t)frames;
    return true;
}

size_t monitor_pcm_link_read_block_for_transport(uint8_t *dst, size_t dst_capacity)
{
    if (!dst || s_queued_blocks == 0u) {
        return 0u;
    }

    const monitor_pcm_link_queued_block_t *slot = &s_queue[s_read_index];
    size_t payload_bytes = (size_t)slot->header.frames * 2u * sizeof(int16_t);
    size_t total_bytes = sizeof(slot->header) + payload_bytes;
    if (dst_capacity < total_bytes) {
        return 0u;
    }

    memcpy(dst, &slot->header, sizeof(slot->header));
    memcpy(dst + sizeof(slot->header), slot->pcm, payload_bytes);
    s_read_index = (s_read_index + 1u) % MONITOR_PCM_LINK_QUEUE_DEPTH;
    s_queued_blocks--;
    return total_bytes;
}

void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out)
{
    if (out) {
        *out = s_stats;
    }
}
