/* JC1060 exact PVBR seek (v263). v262 hardware: after a hot cue or a touch
 * seek the audio trailed the waveform by 100-300 ms, because decoding
 * restarted at the PVBR entry before the target while the deck reported the
 * target. Trace: "PVBR seek 157622 ms -> table[377]" on a ~167 s track. */
#include "audio_seek_skip.h"

#include <assert.h>
#include <stdio.h>

#define LEN 400u

static void test_index_and_entry(void)
{
    const uint32_t dur = 167300u;   /* 418.25 ms per entry */
    assert(audio_pvbr_index(157622u, dur, LEN) == 376u);   /* floor(376.85) */
    uint32_t idx = audio_pvbr_index(31768u, dur, LEN);
    uint32_t entry = audio_pvbr_entry_ms(idx, dur, LEN, 31768u);
    assert(entry <= 31768u);
    assert(31768u - entry < 419u);
    /* Start of track and past the end. */
    assert(audio_pvbr_index(0u, dur, LEN) == 0u);
    assert(audio_pvbr_entry_ms(0u, dur, LEN, 0u) == 0u);
    assert(audio_pvbr_index(dur + 5000u, dur, LEN) == LEN - 1u);
    /* Unknown duration: entry 0, no skip past the target. */
    assert(audio_pvbr_index(1234u, 0u, LEN) == 0u);
    assert(audio_pvbr_entry_ms(0u, 0u, LEN, 1234u) == 0u);
}

static void test_entry_never_past_target(void)
{
    for (uint32_t dur = 1000u; dur < 700000u; dur += 7919u) {
        for (uint32_t pos = 0u; pos <= dur; pos += dur / 97u + 1u) {
            uint32_t idx = audio_pvbr_index(pos, dur, LEN);
            uint32_t entry = audio_pvbr_entry_ms(idx, dur, LEN, pos);
            assert(entry <= pos);
            assert(pos - entry <= dur / LEN + 1u);
        }
    }
}

static void test_skip_frames(void)
{
    assert(audio_seek_skip_frames(31768u, 31368u, 48000u) == 19200u);
    assert(audio_seek_skip_frames(31768u, 31768u, 48000u) == 0u);
    assert(audio_seek_skip_frames(100u, 200u, 48000u) == 0u);
    assert(audio_seek_skip_frames(1000u, 0u, 0u) == 0u);
    assert(audio_seek_skip_frames(1000u, 0u, 44100u) == 44100u);
}

static void test_take_over_batches(void)
{
    /* 1152-frame MP3 batches: whole batches go, the last one is split so the
     * first published frame is the target frame. */
    uint32_t skip = 19200u;
    uint32_t published = 0u, batches = 0u;
    while (published == 0u) {
        uint32_t drop = audio_seek_skip_take(&skip, 1152u);
        published = 1152u - drop;
        batches++;
    }
    assert(batches == 17u);                 /* 16 x 1152 = 18432, +768 */
    assert(published == 1152u - 768u);
    assert(skip == 0u);
    assert(audio_seek_skip_take(&skip, 1152u) == 0u);
    assert(audio_seek_skip_take(NULL, 1152u) == 0u);
}

int main(void)
{
    test_index_and_entry();
    test_entry_never_past_target();
    test_skip_frames();
    test_take_over_batches();
    printf("audio_seek_skip: all tests passed\n");
    return 0;
}
