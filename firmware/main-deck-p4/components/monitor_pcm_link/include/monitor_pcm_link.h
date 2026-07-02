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

esp_err_t monitor_pcm_link_init(void);
esp_err_t monitor_pcm_link_set_format(uint32_t sample_rate,
                                      uint8_t channels,
                                      uint8_t bits_per_sample);
bool monitor_pcm_link_write_nonblocking(const int16_t *interleaved_stereo, size_t frames);
void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out);

#ifdef __cplusplus
}
#endif
