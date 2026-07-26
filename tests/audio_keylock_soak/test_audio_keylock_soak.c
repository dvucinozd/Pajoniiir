#include "audio_keylock.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define OUTPUT_SAMPLE_RATE 48000u
#define FIXTURE_FRAMES 65536u
#define DEFAULT_VIRTUAL_SECONDS 300u
#define MAX_VIRTUAL_SECONDS 3600u
#define CLICK_THRESHOLD 6000
#define FREQUENCY_TOLERANCE_PERCENT 3.0
#define DRIFT_TOLERANCE_FRAMES 16.0

typedef struct {
    audio_mixer_frame_t *frames;
} repeating_source_t;

typedef struct {
    const char *name;
    audio_keylock_t keylock;
    repeating_source_t source;
    float tempo_factor;
    float rate_ratio;
    double expected_frequency_hz;
    uint64_t consumed_total;
    uint64_t positive_crossings;
    uint64_t click_count;
    uint64_t clipped_samples;
    uint64_t play_seq;
    int32_t peak;
    int32_t max_jump;
    int16_t previous_left;
    bool have_previous;
} deck_soak_t;

static audio_mixer_frame_t fixture_deck_1[FIXTURE_FRAMES];
static audio_mixer_frame_t fixture_deck_2[FIXTURE_FRAMES];

static int16_t clamp_i16(double value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)lrint(value);
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void generate_fixture(audio_mixer_frame_t *frames,
                             uint32_t base_cycles,
                             double phase_offset)
{
    const double two_pi = 6.28318530717958647692;
    for (uint32_t i = 0u; i < FIXTURE_FRAMES; i++) {
        double phase = two_pi * (double)base_cycles * (double)i /
                       (double)FIXTURE_FRAMES;
        double left = 8200.0 * sin(phase) +
                      1800.0 * sin(phase * 2.0 + phase_offset);
        double right = 7600.0 * sin(phase + 0.31) +
                       1600.0 * sin(phase * 3.0 + phase_offset);
        frames[i] = (audio_mixer_frame_t) {
            .left = clamp_i16(left),
            .right = clamp_i16(right),
        };
    }
}

static bool read_repeating_source(void *ctx,
                                  uint64_t sequence,
                                  audio_mixer_frame_t *out)
{
    repeating_source_t *source = (repeating_source_t *)ctx;
    if (!source || !source->frames || !out) return false;
    *out = source->frames[sequence % FIXTURE_FRAMES];
    return true;
}

static bool keylock_state_is_finite(const audio_keylock_t *state)
{
    return state &&
           isfinite(state->grain_a) &&
           isfinite(state->grain_b) &&
           isfinite(state->logical_fraction) &&
           isfinite(state->tempo_factor) &&
           isfinite(state->rate_ratio);
}

static void observe_deck_frame(deck_soak_t *deck,
                               const audio_mixer_frame_t *frame)
{
    int32_t left = frame->left;
    int32_t right = frame->right;
    int32_t left_abs = abs_i32(left);
    int32_t right_abs = abs_i32(right);
    if (left_abs > deck->peak) deck->peak = left_abs;
    if (right_abs > deck->peak) deck->peak = right_abs;
    if (left == INT16_MIN || left == INT16_MAX ||
        right == INT16_MIN || right == INT16_MAX) {
        deck->clipped_samples++;
    }

    if (deck->have_previous) {
        int32_t jump = abs_i32(left - (int32_t)deck->previous_left);
        if (jump > deck->max_jump) deck->max_jump = jump;
        if (jump > CLICK_THRESHOLD) deck->click_count++;
        if (deck->previous_left <= 0 && left > 0) {
            deck->positive_crossings++;
        }
    }
    deck->previous_left = frame->left;
    deck->have_previous = true;
}

static audio_mixer_frame_t render_deck(deck_soak_t *deck)
{
    audio_mixer_frame_t frame = {0};
    uint32_t consumed = 0u;
    bool ok = audio_keylock_next(&deck->keylock,
                                 read_repeating_source,
                                 &deck->source,
                                 &frame,
                                 &consumed,
                                 &deck->play_seq);
    assert(ok);
    deck->consumed_total += consumed;
    observe_deck_frame(deck, &frame);
    return frame;
}

static uint32_t parse_virtual_seconds(int argc, char **argv)
{
    if (argc == 1) return DEFAULT_VIRTUAL_SECONDS;
    if (argc != 2) {
        fprintf(stderr, "usage: %s [virtual-seconds]\n", argv[0]);
        exit(2);
    }

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(argv[1], &end, 10);
    if (errno != 0 || !end || *end != '\0' ||
        value == 0u || value > MAX_VIRTUAL_SECONDS) {
        fprintf(stderr, "virtual-seconds must be in range 1..%u\n",
                MAX_VIRTUAL_SECONDS);
        exit(2);
    }
    return (uint32_t)value;
}

static void assert_deck_result(const deck_soak_t *deck,
                               uint64_t output_frames,
                               double virtual_seconds)
{
    double logical_step = (double)(deck->tempo_factor * deck->rate_ratio);
    double expected_consumed = floor((double)output_frames * logical_step);
    double drift = fabs((double)deck->consumed_total - expected_consumed);
    double measured_frequency =
        (double)deck->positive_crossings / virtual_seconds;
    double frequency_error_percent =
        fabs(measured_frequency - deck->expected_frequency_hz) * 100.0 /
        deck->expected_frequency_hz;

    printf("%s frames=%llu consumed=%llu expected=%.0f drift=%.1f "
           "frequency=%.3fHz expected_frequency=%.3fHz error=%.3f%% "
           "peak=%d max_jump=%d clicks=%llu clipped=%llu origin=%llu\n",
           deck->name,
           (unsigned long long)output_frames,
           (unsigned long long)deck->consumed_total,
           expected_consumed,
           drift,
           measured_frequency,
           deck->expected_frequency_hz,
           frequency_error_percent,
           deck->peak,
           deck->max_jump,
           (unsigned long long)deck->click_count,
           (unsigned long long)deck->clipped_samples,
           (unsigned long long)deck->keylock.origin_seq);

    assert(drift <= DRIFT_TOLERANCE_FRAMES);
    assert(frequency_error_percent <= FREQUENCY_TOLERANCE_PERCENT);
    assert(deck->peak > 1000);
    assert(deck->peak < INT16_MAX);
    assert(deck->max_jump <= CLICK_THRESHOLD);
    assert(deck->click_count == 0u);
    assert(deck->clipped_samples == 0u);
    assert(deck->play_seq == deck->consumed_total);
    assert(deck->keylock.origin_seq > 0u);
    assert(keylock_state_is_finite(&deck->keylock));
}

int main(int argc, char **argv)
{
    uint32_t virtual_seconds = parse_virtual_seconds(argc, argv);
    uint64_t output_frames =
        (uint64_t)virtual_seconds * OUTPUT_SAMPLE_RATE;
    const uint32_t deck_1_cycles = 503u;
    const uint32_t deck_2_cycles = 607u;
    const uint32_t deck_2_sample_rate = 44100u;

    generate_fixture(fixture_deck_1, deck_1_cycles, 0.17);
    generate_fixture(fixture_deck_2, deck_2_cycles, 0.73);

    deck_soak_t deck_1 = {
        .name = "deck1",
        .source = { .frames = fixture_deck_1 },
        .tempo_factor = 1.15f,
        .rate_ratio = 1.0f,
        .expected_frequency_hz =
            (double)deck_1_cycles * OUTPUT_SAMPLE_RATE / FIXTURE_FRAMES,
    };
    deck_soak_t deck_2 = {
        .name = "deck2",
        .source = { .frames = fixture_deck_2 },
        .tempo_factor = 0.85f,
        .rate_ratio = (float)deck_2_sample_rate / OUTPUT_SAMPLE_RATE,
        .expected_frequency_hz =
            (double)deck_2_cycles * deck_2_sample_rate / FIXTURE_FRAMES,
    };
    audio_keylock_reset(&deck_1.keylock, 0u);
    audio_keylock_reset(&deck_2.keylock, 0u);
    audio_keylock_configure(&deck_1.keylock,
                            deck_1.tempo_factor,
                            deck_1.rate_ratio);
    audio_keylock_configure(&deck_2.keylock,
                            deck_2.tempo_factor,
                            deck_2.rate_ratio);

    uint64_t mixed_clipped_samples = 0u;
    int32_t mixed_peak = 0;
    clock_t started = clock();
    for (uint64_t i = 0u; i < output_frames; i++) {
        audio_mixer_frame_t frame_1 = render_deck(&deck_1);
        audio_mixer_frame_t frame_2 = render_deck(&deck_2);
        int32_t mixed_left = (int32_t)frame_1.left + frame_2.left;
        int32_t mixed_right = (int32_t)frame_1.right + frame_2.right;
        int32_t left_abs = abs_i32(mixed_left);
        int32_t right_abs = abs_i32(mixed_right);
        if (left_abs > mixed_peak) mixed_peak = left_abs;
        if (right_abs > mixed_peak) mixed_peak = right_abs;
        if (mixed_left < INT16_MIN || mixed_left > INT16_MAX ||
            mixed_right < INT16_MIN || mixed_right > INT16_MAX) {
            mixed_clipped_samples++;
        }

        if ((i & 1023u) == 0u) {
            assert(keylock_state_is_finite(&deck_1.keylock));
            assert(keylock_state_is_finite(&deck_2.keylock));
        }
    }
    double elapsed_seconds =
        (double)(clock() - started) / CLOCKS_PER_SEC;
    double duration = (double)output_frames / OUTPUT_SAMPLE_RATE;

    assert_deck_result(&deck_1, output_frames, duration);
    assert_deck_result(&deck_2, output_frames, duration);
    assert(mixed_clipped_samples == 0u);
    assert(mixed_peak < INT16_MAX);
    printf("dual-deck virtual_duration=%.1fs mixed_peak=%d "
           "mixed_clipped=%llu host_cpu_time=%.3fs\n",
           duration,
           mixed_peak,
           (unsigned long long)mixed_clipped_samples,
           elapsed_seconds);
    puts("audio_keylock dual-deck soak passed");
    return 0;
}
