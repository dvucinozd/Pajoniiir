#include "audio_keylock.h"
#include "audio_pcm_timeline.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_RATE 48000u
#define CAPACITY 8192u
#define RUNWAY 4096u

static audio_mixer_frame_t fixture[MAX_RATE];
static int16_t storage[CAPACITY * 2u];

static bool read_timeline(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    return audio_pcm_timeline_read(ctx, seq, out);
}

static double output_time(double source_time, double change, double restore,
                          double tempo)
{
    if (source_time < change) return source_time;
    double at_restore = change + (restore - change) * tempo;
    if (source_time < at_restore) return change + (source_time - change) / tempo;
    return restore + source_time - at_restore;
}

static void run_tempo_response(unsigned source_rate, unsigned output_rate,
                               float tempo)
{
    const double pi = 3.14159265358979323846;
    unsigned first_onset = 0u;
    for (unsigned i = 0; i < source_rate; ++i) {
        unsigned phase = i % (source_rate / 2u);
        double envelope = phase < source_rate / 25u
            ? 12000.0 * sin(pi * phase / (source_rate / 25u)) : 0.0;
        int16_t sample = (int16_t)((1000.0 + envelope) *
                                  sin(2.0 * pi * 440.0 * i / source_rate));
        fixture[i] = (audio_mixer_frame_t){sample, sample};
        if (first_onset == 0u && abs(sample) > 6000) first_onset = i;
    }

    audio_pcm_timeline_t timeline;
    audio_pcm_timeline_init(&timeline, storage, CAPACITY);
    audio_keylock_t state;
    audio_keylock_reset(&state, 0u);
    uint64_t play_seq = 0u;
    const unsigned change_frame = 2u * output_rate + 73u;
    const unsigned restore_frame = 6u * output_rate + 137u;
    const double change = (double)change_frame / output_rate;
    const double restore = (double)restore_frame / output_rate;
    unsigned last_onset = 0u;
    unsigned onsets = 0u;
    double max_error_ms = 0.0;
    for (unsigned i = 0u; i < 10u * output_rate; ++i) {
        while (audio_pcm_timeline_future_frames(&timeline) < RUNWAY) {
            uint64_t write = audio_pcm_timeline_write_seq(&timeline);
            audio_mixer_frame_t frame = fixture[write % source_rate];
            assert(audio_pcm_timeline_push(&timeline, frame.left, frame.right));
        }
        float current = i >= change_frame && i < restore_frame ? tempo : 1.0f;
        audio_keylock_configure(&state, current,
                                (float)source_rate / output_rate);
        audio_mixer_frame_t frame;
        assert(audio_keylock_next(&state, read_timeline, &timeline,
                                  &frame, NULL, &play_seq));
        assert(audio_pcm_timeline_set_playhead(&timeline, play_seq));
        if (abs(frame.left) > 6000 &&
            (onsets == 0u || i - last_onset > output_rate / 5u)) {
            double source_time = (double)first_onset / source_rate + onsets * 0.5;
            double expected = output_time(source_time, change, restore, tempo);
            double error_ms = fabs((double)i / output_rate - expected) * 1000.0;
            if (error_ms > max_error_ms) max_error_ms = error_ms;
            last_onset = i;
            onsets++;
        }
    }
    double source_end = 10.0 + (restore - change) * ((double)tempo - 1.0);
    unsigned expected_onsets = (unsigned)floor(
        (source_end - (double)first_onset / source_rate) / 0.5) + 1u;
    printf("tempo response %u->%u factor=%.3f onsets=%u/%u max_error=%.3fms\n",
           source_rate, output_rate, tempo, onsets, expected_onsets,
           max_error_ms);
    assert(onsets == expected_onsets);
    assert(max_error_ms < 15.0);
    assert(audio_pcm_timeline_oldest_seq(&timeline) > CAPACITY);
}

int main(void)
{
    const float tempos[] = {1.0f, 1.05f, 0.95f, 1.005f, 0.8f, 1.2f};
    for (unsigned i = 0; i < sizeof(tempos) / sizeof(tempos[0]); ++i) {
        run_tempo_response(48000u, 48000u, tempos[i]);
        run_tempo_response(44100u, 48000u, tempos[i]);
        run_tempo_response(48000u, 44100u, tempos[i]);
    }
    puts("audio_keylock acoustic tempo response tests passed");
    return 0;
}
