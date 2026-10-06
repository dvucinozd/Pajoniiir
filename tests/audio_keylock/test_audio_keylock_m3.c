#include "audio_keylock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

static bool read_repeating_source(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    (void)ctx;
    if (!out) return false;
    *out = source[seq % SOURCE_FRAMES];
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
    /* Cache transparency across overlapping grains, tag wrap, coefficient
     * changes and transport reset. The uncached instance uses the same FIR
     * arithmetic but recomputes every requested source frame. */
    for (unsigned i = 0; i < SOURCE_FRAMES; ++i) {
        source[i].left = (int16_t)(10000 * sin(i * .17));
        source[i].right = (int16_t)(9000 * sin(i * .23));
    }
    static audio_keylock_t cached, uncached;
    const float ratios[] = {48000.0f / 44100.0f, 2.0f, 4.0f, 1.088435f};
    audio_keylock_reset(&cached, 0);
    cached.antialias = cached.dense_correlation = true;
    audio_keylock_reset(&uncached, 0);
    uncached.antialias = uncached.dense_correlation = true;
    for (unsigned mode = 0; mode < 4; ++mode) {
        audio_keylock_configure(&cached, mode & 1u ? .95f : 1.05f, ratios[mode]);
        audio_keylock_configure(&uncached, mode & 1u ? .95f : 1.05f, ratios[mode]);
        for (unsigned i = 0; i < 4000; ++i) {
            audio_mixer_frame_t a, b;
            uint32_t ca, cb;
            uint64_t pa, pb;
            memset(uncached.filter_valid, 0, sizeof(uncached.filter_valid));
            assert(audio_keylock_next(&cached, read_repeating_source, NULL, &a, &ca, &pa));
            assert(audio_keylock_next(&uncached, read_repeating_source, NULL, &b, &cb, &pb));
            assert(a.left == b.left && a.right == b.right && ca == cb && pa == pb);
        }
    }
    audio_keylock_reset(&cached, 1234);
    cached.antialias = cached.dense_correlation = true;
    audio_keylock_configure(&cached, 1.0f, 48000.0f / 44100.0f);
    for (unsigned i = 0; i < 10000; ++i) {
        audio_mixer_frame_t out;
        assert(audio_keylock_next(&cached, read_repeating_source, NULL, &out, NULL, NULL));
    }
    printf("48->44.1 MT FIR evaluations=%u for 10000 output frames\n", cached.filter_evaluations);
    assert(cached.filter_evaluations <= 10886);

    /* A 30 kHz source at 96 kHz must not alias to 18 kHz in 48 kHz MT. */
    for (unsigned i = 0; i < SOURCE_FRAMES; ++i)
        source[i].left = source[i].right = (int16_t)(12000 * sin(6.283185307179586 * 30000 * i / 96000));
    audio_keylock_t filtered;
    audio_keylock_reset(&filtered, 0);
    filtered.antialias = filtered.dense_correlation = true;
    audio_keylock_configure(&filtered, 1.0f, 2.0f);
    double energy = 0;
    for (unsigned i = 0; i < 2000; ++i) {
        audio_mixer_frame_t out;
        uint32_t consumed;
        assert(audio_keylock_next(&filtered, read_source, NULL, &out, &consumed, NULL));
        if (i > 200) energy += (double)out.left * out.left;
    }
    assert(sqrt(energy / 1799) < 85);
    const double pi = 3.14159265358979323846;
    for (uint32_t i = 0; i < SOURCE_FRAMES; i++) {
        int16_t v = (int16_t)(sin(2.0 * pi * 1000.0 * i / SAMPLE_RATE) * 12000.0);
        source[i] = (audio_mixer_frame_t){v, v};
    }

    audio_keylock_t state;
    audio_keylock_reset(&state, 0u);
    state.antialias = state.dense_correlation = true;
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
    state.antialias = state.dense_correlation = true;
    audio_keylock_configure(&state, 1.0f, 1.0f);
    for (int i = 0; i < 1024; i++) {
        audio_mixer_frame_t frame;
        assert(audio_keylock_next(&state, read_source, NULL, &frame, NULL, NULL));
        assert(frame.left == source[i].left);
    }


    /* Coordinates must retain exact long-track position while their float
     * working set rebases back into a small, sub-frame-precise window. */
    const uint64_t long_start = 100000000u;
    audio_keylock_reset(&state, long_start);
    state.antialias = state.dense_correlation = true;
    audio_keylock_configure(&state, 1.0f, 1.0f);
    uint64_t play_seq = long_start;
    for (uint32_t i = 0u; i < 20000u; i++) {
        audio_mixer_frame_t frame;
        assert(audio_keylock_next(&state, read_repeating_source, NULL, &frame,
                                  NULL, &play_seq));
        assert(frame.left == source[(long_start + i) % SOURCE_FRAMES].left);
    }
    assert(play_seq == long_start + 20000u);
    assert(state.origin_seq > long_start);
    assert(state.grain_a < 16384.0f);

    audio_keylock_configure(&state, NAN, INFINITY);
    assert(state.tempo_factor == 1.0f);
    assert(state.rate_ratio == 1.0f);
    puts("audio_keylock tests passed");
    return 0;
}
