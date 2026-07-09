#include "audio_pcm_ring.h"

#define AUDIO_PCM_RING_MASK (AUDIO_PCM_RING_FRAMES - 1u)

/* SPSC ring: the decode task is the sole writer of write_index, the output
 * task is the sole reader (sole writer of read_index). Cross-task/-core
 * visibility uses acquire/release atomics so a push is fully written before the
 * matching write_index bump is observed (and vice-versa for pop). used/free are
 * derived as (write - read), which stays correct across the 32-bit wrap. */
static inline uint32_t ring_load_acquire(const volatile uint32_t *p)
{
    return __atomic_load_n((uint32_t *)(uintptr_t)p, __ATOMIC_ACQUIRE);
}

static inline void ring_store_release(volatile uint32_t *p, uint32_t v)
{
    __atomic_store_n((uint32_t *)p, v, __ATOMIC_RELEASE);
}

void audio_pcm_ring_reset(audio_pcm_ring_t *ring)
{
    if (!ring) return;
    /* Publish read first, then write: a consumer that observes the new write
     * has already seen the matching read, so used never transiently underflows
     * into a huge value during the reset. Must be called with the consumer
     * quiescent (producer-side, e.g. the decode task's own seek handling). */
    ring_store_release(&ring->read_index, 0u);
    ring_store_release(&ring->write_index, 0u);
}

uint32_t audio_pcm_ring_used(const audio_pcm_ring_t *ring)
{
    if (!ring) return 0u;
    uint32_t w = ring_load_acquire(&ring->write_index);
    uint32_t r = ring_load_acquire(&ring->read_index);
    return w - r;
}

uint32_t audio_pcm_ring_free(const audio_pcm_ring_t *ring)
{
    if (!ring) return 0u;
    return AUDIO_PCM_RING_FRAMES - 1u - audio_pcm_ring_used(ring);
}

bool audio_pcm_ring_push(audio_pcm_ring_t *ring, int16_t left, int16_t right)
{
    if (!ring) return false;

    uint32_t w = ring->write_index;   /* producer owns write_index */
    uint32_t r = ring_load_acquire(&ring->read_index);
    if ((AUDIO_PCM_RING_FRAMES - 1u - (w - r)) == 0u) return false;

    uint32_t idx = w & AUDIO_PCM_RING_MASK;
    ring->frames[idx * 2u] = left;
    ring->frames[idx * 2u + 1u] = right;
    ring_store_release(&ring->write_index, w + 1u);
    return true;
}

bool audio_pcm_ring_pop(audio_pcm_ring_t *ring, audio_mixer_frame_t *out_frame)
{
    if (!ring || !out_frame) return false;

    uint32_t r = ring->read_index;    /* consumer owns read_index */
    uint32_t w = ring_load_acquire(&ring->write_index);
    if ((w - r) == 0u) return false;

    uint32_t idx = r & AUDIO_PCM_RING_MASK;
    out_frame->left = ring->frames[idx * 2u];
    out_frame->right = ring->frames[idx * 2u + 1u];
    ring_store_release(&ring->read_index, r + 1u);
    return true;
}
