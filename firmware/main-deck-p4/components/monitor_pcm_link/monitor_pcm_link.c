#include "monitor_pcm_link.h"

#include <string.h>

typedef struct {
    monitor_pcm_link_block_header_t header;
    int16_t pcm[MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK * 2u];
} monitor_pcm_link_queued_block_t;

static monitor_pcm_link_stats_t s_stats;
static bool s_enabled;
static monitor_pcm_link_queued_block_t s_queue[MONITOR_PCM_LINK_QUEUE_DEPTH];
/* SPSC queue: the audio task is the only writer (bumps s_write_count), the
   transport task is the only reader (bumps s_read_count). Cross-task
   visibility uses acquire/release atomics; used depth = write - read. */
static uint32_t s_write_count;
static uint32_t s_read_count;
static uint32_t s_next_sequence;

static uint32_t monitor_pcm_link_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
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

static uint32_t monitor_pcm_link_block_crc32(const monitor_pcm_link_block_header_t *header,
                                             const int16_t *pcm,
                                             size_t payload_bytes)
{
    monitor_pcm_link_block_header_t protected_header = *header;
    protected_header.block_crc32 = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    crc = monitor_pcm_link_crc32_update(crc,
                                        (const uint8_t *)&protected_header,
                                        sizeof(protected_header));
    crc = monitor_pcm_link_crc32_update(crc, (const uint8_t *)pcm, payload_bytes);
    return ~crc;
}

esp_err_t monitor_pcm_link_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_queue, 0, sizeof(s_queue));
    s_enabled = false;
    __atomic_store_n(&s_write_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_read_count, 0u, __ATOMIC_RELEASE);
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
    if (!s_enabled) {
        /* Feature off — not a drop; don't inflate the diagnostics counter. */
        return false;
    }
    if (!interleaved_stereo || frames == 0u || frames > MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK) {
        s_stats.dropped_blocks++;
        return false;
    }
    if (s_stats.sample_rate == 0u || s_stats.channels != 2u || s_stats.bits_per_sample != 16u) {
        s_stats.dropped_blocks++;
        return false;
    }
    const uint32_t write_count = s_write_count;
    const uint32_t read_count = __atomic_load_n(&s_read_count, __ATOMIC_ACQUIRE);
    if (write_count - read_count >= MONITOR_PCM_LINK_QUEUE_DEPTH) {
        s_stats.dropped_blocks++;
        return false;
    }

    monitor_pcm_link_queued_block_t *slot = &s_queue[write_count % MONITOR_PCM_LINK_QUEUE_DEPTH];
    size_t payload_bytes = frames * 2u * sizeof(int16_t);
    memcpy(slot->pcm, interleaved_stereo, payload_bytes);
    slot->header.magic = MONITOR_PCM_LINK_BLOCK_MAGIC;
    slot->header.header_bytes = (uint16_t)sizeof(monitor_pcm_link_block_header_t);
    slot->header.frames = (uint16_t)frames;
    slot->header.sample_rate = s_stats.sample_rate;
    slot->header.sequence = s_next_sequence++;
    slot->header.block_crc32 = monitor_pcm_link_block_crc32(&slot->header,
                                                            slot->pcm,
                                                            payload_bytes);

    __atomic_store_n(&s_write_count, write_count + 1u, __ATOMIC_RELEASE);
    s_stats.submitted_blocks++;
    s_stats.submitted_frames += (uint32_t)frames;
    return true;
}

size_t monitor_pcm_link_read_block_for_transport(uint8_t *dst, size_t dst_capacity)
{
    if (!dst) {
        return 0u;
    }

    const uint32_t read_count = s_read_count;
    const uint32_t write_count = __atomic_load_n(&s_write_count, __ATOMIC_ACQUIRE);
    if (write_count == read_count) {
        return 0u;
    }

    const monitor_pcm_link_queued_block_t *slot = &s_queue[read_count % MONITOR_PCM_LINK_QUEUE_DEPTH];
    size_t payload_bytes = (size_t)slot->header.frames * 2u * sizeof(int16_t);
    size_t total_bytes = sizeof(slot->header) + payload_bytes;
    if (dst_capacity < total_bytes) {
        return 0u;
    }

    memcpy(dst, &slot->header, sizeof(slot->header));
    memcpy(dst + sizeof(slot->header), slot->pcm, payload_bytes);
    __atomic_store_n(&s_read_count, read_count + 1u, __ATOMIC_RELEASE);
    return total_bytes;
}

void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out)
{
    if (out) {
        *out = s_stats;
    }
}
