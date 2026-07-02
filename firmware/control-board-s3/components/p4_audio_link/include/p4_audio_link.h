#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P4_AUDIO_LINK_BLOCK_MAGIC 0x50483450u /* 'P4HP' little endian */
#define P4_AUDIO_LINK_RING_CAPACITY_FRAMES 4096u

typedef struct {
    uint32_t magic;
    uint16_t header_bytes;
    uint16_t frames;
    uint32_t sample_rate;
    uint32_t sequence;
    uint32_t payload_crc32;
} p4_audio_link_block_header_t;

typedef struct {
    uint32_t received_blocks;
    uint32_t sequence_gaps;
    uint32_t crc_errors;
    uint32_t ring_frames;
    uint32_t ring_capacity_frames;
    uint32_t underruns;
    uint32_t overruns;
    uint32_t sample_rate;
} p4_audio_link_stats_t;

esp_err_t p4_audio_link_init(void);
bool p4_audio_link_receive_block(const uint8_t *block, size_t block_len);
size_t p4_audio_link_read_frames(int16_t *dst_interleaved_stereo, size_t frames);
void p4_audio_link_get_stats(p4_audio_link_stats_t *out);

#ifdef __cplusplus
}
#endif
