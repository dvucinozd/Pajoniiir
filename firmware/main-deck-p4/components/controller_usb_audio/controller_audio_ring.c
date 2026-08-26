/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_audio_ring.h"
#include <string.h>

bool controller_audio_ring_init(controller_audio_ring_t *ring,
                                int16_t *storage,
                                uint32_t frame_capacity,
                                uint8_t channels,
                                uint32_t sample_rate)
{
    if (!ring || !storage || frame_capacity == 0u ||
        channels == 0u || channels > 8u || sample_rate == 0u) {
        return false;
    }
    memset(ring, 0, sizeof(*ring));
    ring->samples = storage;
    ring->frame_capacity = frame_capacity;
    ring->channels = channels;
    ring->sample_rate = sample_rate;
    ring->generation = 1u;
    return true;
}

void controller_audio_ring_reset(controller_audio_ring_t *ring,
                                 uint32_t sample_rate)
{
    if (!ring || !ring->samples || sample_rate == 0u) {
        return;
    }
    ring->read_frame = 0u;
    ring->write_frame = 0u;
    ring->queued_frames = 0u;
    ring->sample_rate = sample_rate;
    ring->generation++;
    ring->overrun_frames = 0u;
    ring->underrun_frames = 0u;
    ring->high_water_frames = 0u;
    ring->clock_trimmed_frames = 0u;
    ring->clock_duplicated_frames = 0u;
}

uint32_t controller_audio_ring_write(controller_audio_ring_t *ring,
                                     const int16_t *interleaved,
                                     uint32_t frames)
{
    if (!ring || !ring->samples || !interleaved || frames == 0u) {
        return 0u;
    }
    const uint32_t free_frames = controller_audio_ring_free(ring);
    const uint32_t accepted = frames < free_frames ? frames : free_frames;
    for (uint32_t frame = 0u; frame < accepted; ++frame) {
        const uint32_t dst_frame = (ring->write_frame + frame) % ring->frame_capacity;
        memcpy(&ring->samples[(size_t)dst_frame * ring->channels],
               &interleaved[(size_t)frame * ring->channels],
               (size_t)ring->channels * sizeof(int16_t));
    }
    ring->write_frame = (ring->write_frame + accepted) % ring->frame_capacity;
    ring->queued_frames += accepted;
    if (ring->queued_frames > ring->high_water_frames) {
        ring->high_water_frames = ring->queued_frames;
    }
    ring->written_frames += accepted;
    ring->overrun_frames += (uint64_t)(frames - accepted);
    return accepted;
}

uint32_t controller_audio_ring_write_clocked(controller_audio_ring_t *ring,
                                             const int16_t *interleaved,
                                             uint32_t frames)
{
    if (!ring || !ring->samples || !interleaved || frames == 0u) {
        return 0u;
    }

    /* The I2S producer and USB SOF consumer use independent clocks. Keep a
     * wide dead band around half-full and slip no more than one frame per
     * producer block outside it. */
    const uint32_t low_water = (ring->frame_capacity * 3u) / 8u;
    const uint32_t high_water = (ring->frame_capacity * 5u) / 8u;
    const uint32_t free_frames = controller_audio_ring_free(ring);

    if (ring->queued_frames >= high_water && frames > 1u) {
        ring->clock_trimmed_frames++;
        return controller_audio_ring_write(ring, interleaved, frames - 1u);
    }

    const bool duplicate =
        ring->queued_frames <= low_water && free_frames > frames;
    uint32_t written = controller_audio_ring_write(ring, interleaved, frames);
    if (duplicate && written == frames) {
        const int16_t *last =
            &interleaved[(size_t)(frames - 1u) * ring->channels];
        if (controller_audio_ring_write(ring, last, 1u) == 1u) {
            ring->clock_duplicated_frames++;
            written++;
        }
    }
    return written;
}

uint32_t controller_audio_ring_read(controller_audio_ring_t *ring,
                                    int16_t *interleaved,
                                    uint32_t frames,
                                    bool zero_fill)
{
    if (!ring || !ring->samples || !interleaved || frames == 0u) {
        return 0u;
    }
    const uint32_t available =
        frames < ring->queued_frames ? frames : ring->queued_frames;
    for (uint32_t frame = 0u; frame < available; ++frame) {
        const uint32_t src_frame = (ring->read_frame + frame) % ring->frame_capacity;
        memcpy(&interleaved[(size_t)frame * ring->channels],
               &ring->samples[(size_t)src_frame * ring->channels],
               (size_t)ring->channels * sizeof(int16_t));
    }
    if (zero_fill && available < frames) {
        memset(&interleaved[(size_t)available * ring->channels], 0,
               (size_t)(frames - available) * ring->channels * sizeof(int16_t));
    }
    ring->read_frame = (ring->read_frame + available) % ring->frame_capacity;
    ring->queued_frames -= available;
    ring->read_frames += available;
    ring->underrun_frames += (uint64_t)(frames - available);
    return available;
}

uint32_t controller_audio_ring_queued(const controller_audio_ring_t *ring)
{
    return ring ? ring->queued_frames : 0u;
}

uint32_t controller_audio_ring_free(const controller_audio_ring_t *ring)
{
    return ring && ring->frame_capacity >= ring->queued_frames
               ? ring->frame_capacity - ring->queued_frames
               : 0u;
}
