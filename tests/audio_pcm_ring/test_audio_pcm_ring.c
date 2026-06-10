#include "audio_pcm_ring.h"
#include <assert.h>
#include <stdio.h>

static void test_reset_starts_empty(void)
{
    audio_pcm_ring_t ring;
    audio_pcm_ring_reset(&ring);

    assert(audio_pcm_ring_used(&ring) == 0);
    assert(audio_pcm_ring_free(&ring) == AUDIO_PCM_RING_FRAMES - 1u);
}

static void test_push_pop_preserves_stereo_order(void)
{
    audio_pcm_ring_t ring;
    audio_pcm_ring_reset(&ring);

    assert(audio_pcm_ring_push(&ring, 100, -100));
    assert(audio_pcm_ring_push(&ring, 200, -200));
    assert(audio_pcm_ring_used(&ring) == 2);

    audio_mixer_frame_t frame = { 0 };
    assert(audio_pcm_ring_pop(&ring, &frame));
    assert(frame.left == 100);
    assert(frame.right == -100);

    assert(audio_pcm_ring_pop(&ring, &frame));
    assert(frame.left == 200);
    assert(frame.right == -200);

    assert(!audio_pcm_ring_pop(&ring, &frame));
}

static void test_full_ring_rejects_push_without_overwrite(void)
{
    audio_pcm_ring_t ring;
    audio_pcm_ring_reset(&ring);

    for (uint32_t i = 0; i < AUDIO_PCM_RING_FRAMES - 1u; i++) {
        assert(audio_pcm_ring_push(&ring, (int16_t)i, (int16_t)-i));
    }

    assert(audio_pcm_ring_free(&ring) == 0);
    assert(!audio_pcm_ring_push(&ring, 1234, 5678));

    audio_mixer_frame_t frame = { 0 };
    assert(audio_pcm_ring_pop(&ring, &frame));
    assert(frame.left == 0);
    assert(frame.right == 0);
}

int main(void)
{
    test_reset_starts_empty();
    test_push_pop_preserves_stereo_order();
    test_full_ring_rejects_push_without_overwrite();
    puts("audio_pcm_ring tests passed");
    return 0;
}
