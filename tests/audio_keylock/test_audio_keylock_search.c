#include "audio_keylock.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define FIXTURE 8192u

static audio_mixer_frame_t frames[FIXTURE];

typedef struct {
    unsigned calls;
    bool missing;
} source_t;

static bool read_source(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    source_t *source = ctx;
    source->calls++;
    if (source->missing && seq % FIXTURE >= FIXTURE - 3u) return false;
    *out = frames[seq % FIXTURE];
    return true;
}

static void run(float ratio, float tempo, bool missing)
{
    struct {
        uint64_t before;
        audio_keylock_t state;
        uint64_t after;
    } guarded = {
        .before = UINT64_C(0x123456789abcdef0),
        .after = UINT64_C(0xfedcba9876543210),
    };
    audio_keylock_t *state = &guarded.state;
    source_t source = { .missing = missing };
    audio_keylock_reset(state, UINT64_C(100000000));
    unsigned max_calls = 0u;
    unsigned max_candidates = 0u;
    unsigned failures = 0u;
    for (unsigned i = 0; i < 48000u; ++i) {
        float current = i < 24000u ? tempo :
                        (i < 36000u ? ((i / 73u) % 2u ? 1.05f : 0.95f) : 1.0f);
        audio_keylock_configure(state, current, ratio);
        audio_mixer_frame_t frame;
        source.calls = 0u;
        bool ok = audio_keylock_next(state, read_source, &source,
                                     &frame, NULL, NULL);
        if (source.calls > max_calls) max_calls = source.calls;
        if (state->last_search_candidates > max_candidates) {
            max_candidates = state->last_search_candidates;
        }
        if (!ok) {
            assert(missing);
            failures++;
            audio_keylock_reset(state, state->logical_seq + 1024u);
        }
        assert(guarded.before == UINT64_C(0x123456789abcdef0));
        assert(guarded.after == UINT64_C(0xfedcba9876543210));
    }
    unsigned radius = (unsigned)(48.0f * ratio + 0.5f);
    if (radius < 12u) radius = 12u;
    unsigned limit = 2u * radius + (unsigned)ceilf(60.0f * ratio) + 40u;
    printf("search ratio=%.6f tempo=%.3f missing=%u failures=%u "
           "max_reads=%u limit=%u max_candidates=%u\n",
           ratio, tempo, missing, failures, max_calls, limit, max_candidates);
    assert(max_calls <= limit);
    assert(max_calls <= 64u);
    /* Two simultaneous Master Tempo decks share one output deadline. Keep
     * each WSOLA alignment search to one bounded coarse pass plus a small
     * local refinement so a high-rate deck cannot starve decode. */
    assert(max_candidates <= 16u);
}

int main(void)
{
    const double pi = 3.14159265358979323846;
    for (unsigned i = 0; i < FIXTURE; ++i) {
        double phase = 2.0 * pi * i / FIXTURE;
        frames[i].left = (int16_t)(10000.0 * sin(phase * 71.0) +
                                  4000.0 * sin(phase * 179.0));
        frames[i].right = (int16_t)(9000.0 * sin(phase * 83.0) +
                                   3000.0 * cos(phase * 197.0));
    }
    const float ratios[] = {0.25f, 44100.0f / 48000.0f, 1.0f,
                            48000.0f / 44100.0f, 96000.0f / 44100.0f, 4.0f};
    for (unsigned r = 0; r < sizeof(ratios) / sizeof(ratios[0]); ++r) {
        run(ratios[r], 0.95f, false);
        run(ratios[r], 1.05f, false);
        run(ratios[r], 0.8f, true);
    }
    puts("audio_keylock search read budget tests passed");
    return 0;
}
