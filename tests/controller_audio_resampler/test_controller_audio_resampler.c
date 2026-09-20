#include "controller_audio_resampler.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    CHANNELS = 4,
    FIRST_FRAMES = 128,
    SECOND_FRAMES = 17,
};

static void fill_frames(int16_t *samples, size_t frames, int16_t base)
{
    for (size_t frame = 0u; frame < frames; ++frame) {
        for (size_t channel = 0u; channel < CHANNELS; ++channel) {
            samples[frame * CHANNELS + channel] =
                (int16_t)(base + (int16_t)(frame * 11u + channel));
        }
    }
}

static void test_equal_rate_fast_path(void)
{
    int16_t first[FIRST_FRAMES * CHANNELS];
    int16_t first_out[FIRST_FRAMES * CHANNELS];
    int16_t second[SECOND_FRAMES * CHANNELS];
    int16_t second_out[SECOND_FRAMES * CHANNELS];
    fill_frames(first, FIRST_FRAMES, -2000);
    fill_frames(second, SECOND_FRAMES, 3000);

    controller_audio_resampler_t resampler;
    assert(controller_audio_resampler_init(
        &resampler, 44100u, 44100u, CHANNELS));
    assert(controller_audio_resampler_output_bound(
        44100u, 44100u, FIRST_FRAMES) == FIRST_FRAMES);

    /* Exact input-sized capacity distinguishes the direct copy from the old
     * generic path, whose conservative bound required one surplus frame. */
    assert(controller_audio_resampler_process(
        &resampler, first, FIRST_FRAMES, first_out, FIRST_FRAMES) ==
        FIRST_FRAMES);
    assert(memcmp(first, first_out, sizeof(first)) == 0);
    assert(resampler.has_previous);
    assert(resampler.input_frames_seen == FIRST_FRAMES);
    assert(resampler.next_output_time ==
           (uint64_t)FIRST_FRAMES * 44100u);
    assert(memcmp(resampler.previous,
                  &first[(FIRST_FRAMES - 1u) * CHANNELS],
                  CHANNELS * sizeof(first[0])) == 0);

    assert(controller_audio_resampler_process(
        &resampler, second, SECOND_FRAMES, second_out, SECOND_FRAMES) ==
        SECOND_FRAMES);
    assert(memcmp(second, second_out, sizeof(second)) == 0);
    assert(resampler.input_frames_seen == FIRST_FRAMES + SECOND_FRAMES);
    assert(resampler.next_output_time ==
           (uint64_t)(FIRST_FRAMES + SECOND_FRAMES) * 44100u);
}

static void test_rate_conversion_still_uses_bounded_path(void)
{
    int16_t input[FIRST_FRAMES * CHANNELS];
    int16_t output[FIRST_FRAMES * CHANNELS];
    fill_frames(input, FIRST_FRAMES, 100);

    controller_audio_resampler_t resampler;
    assert(controller_audio_resampler_init(
        &resampler, 48000u, 44100u, CHANNELS));
    const size_t bound = controller_audio_resampler_output_bound(
        48000u, 44100u, FIRST_FRAMES);
    assert(bound > 0u && bound < FIRST_FRAMES);
    assert(controller_audio_resampler_process(
        &resampler, input, FIRST_FRAMES, output, bound - 1u) == 0u);
    assert(!resampler.has_previous);
    assert(controller_audio_resampler_process(
        &resampler, input, FIRST_FRAMES, output, FIRST_FRAMES) > 0u);
}

int main(void)
{
    test_equal_rate_fast_path();
    test_rate_conversion_still_uses_bounded_path();
    puts("controller audio resampler tests passed");
    return 0;
}
