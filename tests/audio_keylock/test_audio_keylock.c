#include "audio_keylock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define SAMPLE_RATE 48000u
#define SOURCE_FRAMES 8192u
static audio_mixer_frame_t source[SOURCE_FRAMES];

static bool read_source(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    (void)ctx;
    if (!out || seq >= SOURCE_FRAMES) return false;
    *out = source[seq];
    return true;
}

static int count_positive_crossings(const int16_t *samples, int count)
{
    int crossings = 0;
    for (int i = 1; i < count; i++) {
        if (samples[i - 1] <= 0 && samples[i] > 0) crossings++;
    }
    return crossings;
}

int main(void)
{
    const double pi = 3.14159265358979323846;
    for (uint32_t i = 0; i < SOURCE_FRAMES; i++) {
        int16_t v = (int16_t)(sin(2.0 * pi * 1000.0 * i / SAMPLE_RATE) * 12000.0);
        source[i] = (audio_mixer_frame_t){v, v};
    }

    audio_keylock_t state;
    audio_keylock_reset(&state, 0u);
    audio_keylock_configure(&state, 1.10f, 1.0f);
    int16_t output[4096];
    uint32_t consumed_total = 0u;
    for (int i = 0; i < 4096; i++) {
        audio_mixer_frame_t frame;
        uint32_t consumed = 0u;
        uint64_t play_seq = 0u;
        assert(audio_keylock_next(&state, read_source, NULL, &frame,
                                  &consumed, &play_seq));
        output[i] = frame.left;
        consumed_total += consumed;
    }
    /* 4096 output frames at +10% tempo advance about 4506 source frames. */
    assert(consumed_total >= 4504u && consumed_total <= 4507u);
    /* Ignore the first grain; key-lock should retain approximately 1 kHz
     * instead of the 1.1 kHz produced by ordinary rate resampling. */
    int crossings = count_positive_crossings(output + 512, 3072);
    printf("key-lock crossings=%d consumed=%u\n", crossings, consumed_total);
    fflush(stdout);
    assert(crossings >= 60 && crossings <= 68);

    audio_keylock_reset(&state, 0u);
    audio_keylock_configure(&state, 1.0f, 1.0f);
    for (int i = 0; i < 1024; i++) {
        audio_mixer_frame_t frame;
        assert(audio_keylock_next(&state, read_source, NULL, &frame, NULL, NULL));
        assert(frame.left == source[i].left);
    }
    puts("audio_keylock tests passed");
    return 0;
}
