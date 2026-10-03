/* Decode/publish simulation against the actual retained-PCM timeline.
 * Regression: a whole-cap preroll stalls below the cue with no output. */
#include "audio_cue_preroll.h"
#include "audio_pcm_timeline.h"
#include <assert.h>
#include <stdio.h>

#define CAPACITY 196608u
#define MIN_ROOM 2304u
#define BATCH 1152u
static int16_t storage[CAPACITY * 2u];

static void check_rate(uint32_t rate, uint32_t target, bool short_eof)
{
    audio_pcm_timeline_t t;
    audio_pcm_timeline_init(&t, storage, CAPACITY);
    uint32_t cap = rate * 2u;
    if (cap > CAPACITY) cap = CAPACITY;
    audio_cue_preroll_t p;
    uint32_t start = audio_cue_preroll_arm(&p, true, target, rate, cap - MIN_ROOM);
    assert(start <= target);
    assert(p.frames <= cap - MIN_ROOM);
    uint32_t expected = p.frames;
    uint32_t limit = short_eof ? p.frames / 2u : p.frames + BATCH;
    uint32_t n = 0;
    while (n < limit) {
        assert(cap - audio_pcm_timeline_future_frames(&t) >= MIN_ROOM);
        for (unsigned i = 0; i < BATCH && n < limit; ++i, ++n) {
            assert(audio_pcm_timeline_push(&t, (int16_t)(n % 30000), 0));
            uint64_t ph;
            if (audio_cue_preroll_publish_point(&p, audio_pcm_timeline_write_seq(&t), false, &ph)) {
                assert(ph == expected);
                assert(audio_pcm_timeline_set_playhead(&t, ph));
                p.pending = false;
            }
            /* Immediate PLAY cannot consume history while pending. */
            if (p.pending) assert(audio_pcm_timeline_play_seq(&t) == 0);
        }
    }
    if (short_eof) {
        uint64_t ph;
        assert(audio_cue_preroll_publish_point(&p, n, true, &ph));
        assert(ph == n);
        assert(audio_pcm_timeline_set_playhead(&t, ph));
        p.pending = false;
        assert(audio_pcm_timeline_future_frames(&t) == 0);
    } else {
        assert(!p.pending);
        audio_mixer_frame_t frame;
        assert(audio_pcm_timeline_pop(&t, &frame));
        assert(frame.left == (int16_t)(expected % 30000));
        assert(audio_pcm_timeline_history_frames(&t) >= expected);
    }
}

int main(void)
{
    const uint32_t rates[] = {22050, 44100, 48000, 96000, 192000};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        check_rate(rates[i], 15000, false);
        check_rate(rates[i], 50, false);
        check_rate(rates[i], 15000, true);
    }
    audio_cue_preroll_t p;
    assert(audio_cue_preroll_arm(&p, true, 0, 48000, 1000) == 0 && !p.pending);
    assert(audio_cue_preroll_arm(&p, true, 100, 0, 1000) == 100 && !p.pending);
    assert(audio_cue_preroll_arm(&p, false, 100, 48000, 1000) == 100 && !p.pending);
    assert(audio_cue_preroll_arm(&p, true, 100, 48000, 0) == 100 && !p.pending);
    /* Old full-cap policy reproduces the paused producer stall at 48 kHz. */
    audio_cue_preroll_arm(&p, true, 15000, 48000, 96000);
    uint32_t written = 0;
    while (96000 - written >= MIN_ROOM) written += BATCH;
    uint64_t ph;
    assert(written < p.frames);
    assert(!audio_cue_preroll_publish_point(&p, written, false, &ph));
    puts("audio_cue_preroll tests passed");
    return 0;
}
