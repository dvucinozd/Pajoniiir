#include "audio_pcm_ring.h"

#define AUDIO_PCM_RING_MASK (AUDIO_PCM_RING_FRAMES - 1u)

void audio_pcm_ring_reset(audio_pcm_ring_t *ring)
{
    if (!ring) return;
    ring->write_index = 0u;
    ring->read_index = 0u;
}

uint32_t audio_pcm_ring_used(const audio_pcm_ring_t *ring)
{
    if (!ring) return 0u;
    return ring->write_index - ring->read_index;
}

uint32_t audio_pcm_ring_free(const audio_pcm_ring_t *ring)
{
    if (!ring) return 0u;
    return AUDIO_PCM_RING_FRAMES - 1u - audio_pcm_ring_used(ring);
}

bool audio_pcm_ring_push(audio_pcm_ring_t *ring, int16_t left, int16_t right)
{
    if (!ring || audio_pcm_ring_free(ring) == 0u) return false;

    uint32_t idx = ring->write_index & AUDIO_PCM_RING_MASK;
    ring->frames[idx * 2u] = left;
    ring->frames[idx * 2u + 1u] = right;
    ring->write_index++;
    return true;
}

bool audio_pcm_ring_pop(audio_pcm_ring_t *ring, audio_mixer_frame_t *out_frame)
{
    if (!ring || !out_frame || audio_pcm_ring_used(ring) == 0u) return false;

    uint32_t idx = ring->read_index & AUDIO_PCM_RING_MASK;
    out_frame->left = ring->frames[idx * 2u];
    out_frame->right = ring->frames[idx * 2u + 1u];
    ring->read_index++;
    return true;
}
