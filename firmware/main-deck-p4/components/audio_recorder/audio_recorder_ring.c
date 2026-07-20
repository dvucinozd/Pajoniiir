#include "audio_recorder_ring.h"

#include <string.h>

void audio_recorder_ring_init(audio_recorder_ring_t *ring,
                              audio_recorder_block_t *slots, uint32_t capacity)
{
    if (!ring) {
        return;
    }
    ring->slots = slots;
    ring->capacity = capacity;
    ring->write_seq = 0u;
    ring->read_seq = 0u;
    ring->high_water = 0u;
    ring->dropped_blocks = 0u;
    ring->dropped_frames = 0u;
    ring->pushed_blocks = 0u;
}

bool audio_recorder_ring_push(audio_recorder_ring_t *ring,
                              const int16_t *stereo, uint32_t frames,
                              uint32_t sample_rate)
{
    if (!ring || !ring->slots || ring->capacity == 0u || !stereo) {
        return false;
    }

    uint32_t w = __atomic_load_n(&ring->write_seq, __ATOMIC_RELAXED);   /* producer owns */
    uint32_t r = __atomic_load_n(&ring->read_seq, __ATOMIC_ACQUIRE);
    if ((w - r) >= ring->capacity) {
        ring->dropped_blocks++;
        ring->dropped_frames += frames;
        return false;
    }

    if (frames > AUDIO_RECORDER_BLOCK_FRAMES) {
        frames = AUDIO_RECORDER_BLOCK_FRAMES;
    }
    audio_recorder_block_t *slot = &ring->slots[w % ring->capacity];
    slot->sample_rate = sample_rate;
    slot->frames = frames;
    memcpy(slot->samples, stereo, (size_t)frames * 2u * sizeof(int16_t));

    __atomic_store_n(&ring->write_seq, w + 1u, __ATOMIC_RELEASE);

    uint32_t used = (w + 1u) - r;
    if (used > ring->high_water) {
        ring->high_water = used;
    }
    ring->pushed_blocks++;
    return true;
}

const audio_recorder_block_t *audio_recorder_ring_peek(const audio_recorder_ring_t *ring)
{
    if (!ring || !ring->slots) {
        return NULL;
    }
    uint32_t r = __atomic_load_n(&ring->read_seq, __ATOMIC_RELAXED);    /* consumer owns */
    uint32_t w = __atomic_load_n(&ring->write_seq, __ATOMIC_ACQUIRE);
    if (r == w) {
        return NULL;
    }
    return &ring->slots[r % ring->capacity];
}

void audio_recorder_ring_consume(audio_recorder_ring_t *ring)
{
    if (!ring) {
        return;
    }
    uint32_t r = __atomic_load_n(&ring->read_seq, __ATOMIC_RELAXED);
    uint32_t w = __atomic_load_n(&ring->write_seq, __ATOMIC_ACQUIRE);
    if (r == w) {
        return;   /* nothing to consume */
    }
    __atomic_store_n(&ring->read_seq, r + 1u, __ATOMIC_RELEASE);
}

uint32_t audio_recorder_ring_used(const audio_recorder_ring_t *ring)
{
    if (!ring) {
        return 0u;
    }
    uint32_t w = __atomic_load_n(&ring->write_seq, __ATOMIC_ACQUIRE);
    uint32_t r = __atomic_load_n(&ring->read_seq, __ATOMIC_ACQUIRE);
    return w - r;
}
