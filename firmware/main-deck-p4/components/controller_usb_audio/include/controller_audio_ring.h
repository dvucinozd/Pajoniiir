/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t *samples;
    uint32_t frame_capacity;
    uint32_t read_frame;
    uint32_t write_frame;
    uint32_t queued_frames;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t generation;
    uint64_t written_frames;
    uint64_t read_frames;
    uint64_t overrun_frames;
    uint64_t underrun_frames;
} controller_audio_ring_t;

bool controller_audio_ring_init(controller_audio_ring_t *ring,
                                int16_t *storage,
                                uint32_t frame_capacity,
                                uint8_t channels,
                                uint32_t sample_rate);
void controller_audio_ring_reset(controller_audio_ring_t *ring,
                                 uint32_t sample_rate);
uint32_t controller_audio_ring_write(controller_audio_ring_t *ring,
                                     const int16_t *interleaved,
                                     uint32_t frames);
uint32_t controller_audio_ring_read(controller_audio_ring_t *ring,
                                    int16_t *interleaved,
                                    uint32_t frames,
                                    bool zero_fill);
uint32_t controller_audio_ring_queued(const controller_audio_ring_t *ring);
uint32_t controller_audio_ring_free(const controller_audio_ring_t *ring);

#ifdef __cplusplus
}
#endif
