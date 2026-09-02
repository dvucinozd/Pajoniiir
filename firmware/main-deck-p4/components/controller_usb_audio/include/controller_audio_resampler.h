/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stateful linear converter with exact rational phase tracking. Splitting one
 * source stream across arbitrary producer blocks must not introduce drift or
 * a discontinuity at a block boundary. */
typedef struct {
    uint32_t source_rate;
    uint32_t target_rate;
    uint64_t input_frames_seen;
    uint64_t next_output_time;
    uint8_t channels;
    bool has_previous;
    int16_t previous[8];
} controller_audio_resampler_t;

bool controller_audio_resampler_init(controller_audio_resampler_t *resampler,
                                     uint32_t source_rate,
                                     uint32_t target_rate,
                                     uint8_t channels);
size_t controller_audio_resampler_output_bound(uint32_t source_rate,
                                               uint32_t target_rate,
                                               size_t input_frames);
size_t controller_audio_resampler_process(controller_audio_resampler_t *resampler,
                                          const int16_t *input,
                                          size_t input_frames,
                                          int16_t *output,
                                          size_t output_capacity_frames);

#ifdef __cplusplus
}
#endif
