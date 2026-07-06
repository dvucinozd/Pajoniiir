#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t submitted_blocks;
    uint32_t dropped_blocks;
    uint32_t submitted_frames;
} monitor_pcm_link_stats_t;

#define MONITOR_PCM_LINK_BLOCK_MAGIC 0x50483450u /* 'P4HP' little endian */
#define MONITOR_PCM_LINK_QUEUE_DEPTH 8u
#define MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK 256u

typedef struct {
    uint32_t magic;
    uint16_t header_bytes;
    uint16_t frames;
    uint32_t sample_rate;
    uint32_t sequence;
    uint32_t block_crc32;
} monitor_pcm_link_block_header_t;

esp_err_t monitor_pcm_link_init(void);
/* ESP-only, available when CONFIG_MONITOR_PCM_LINK_ENABLED=y: starts the I2S
   TX transport task (and the bench tone task when the bench option is set). */
esp_err_t monitor_pcm_link_start_transport(void);
esp_err_t monitor_pcm_link_set_format(uint32_t sample_rate,
                                      uint8_t channels,
                                      uint8_t bits_per_sample);
void monitor_pcm_link_set_enabled(bool enabled);
bool monitor_pcm_link_write_nonblocking(const int16_t *interleaved_stereo, size_t frames);
size_t monitor_pcm_link_read_block_for_transport(uint8_t *dst, size_t dst_capacity);
void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out);

#ifdef __cplusplus
}
#endif
