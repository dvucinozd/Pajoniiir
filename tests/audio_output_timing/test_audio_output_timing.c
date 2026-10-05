#include "audio_output_timing.h"

#include <assert.h>
#include <stdio.h>

static void test_block_period_us_uses_precise_ceil_division(void)
{
    assert(audio_output_block_period_us(48000) == 5334u);
    assert(audio_output_block_period_us(44100) == 5805u);
    assert(audio_output_block_period_us(32000) == 8000u);
    assert(audio_output_block_period_us(0) == 0u);
}

static void test_late_warning_threshold_allows_codec_write_pacing_slack(void)
{
    assert(audio_output_late_warning_threshold_us(48000) == 10668u);
    assert(audio_output_late_warning_threshold_us(44100) == 11610u);
    assert(audio_output_late_warning_threshold_us(32000) == 16000u);
    assert(audio_output_late_warning_threshold_us(0) == 0u);
}

static void test_continuous_output_periodically_forces_an_idle_tick(void)
{
    assert(!audio_output_should_force_idle(0u, 0u));
    assert(!audio_output_should_force_idle(AUDIO_OUTPUT_MAX_BUSY_BLOCKS - 1u,
                                           AUDIO_OUTPUT_MAX_BUSY_US - 1u));
    assert(audio_output_should_force_idle(AUDIO_OUTPUT_MAX_BUSY_BLOCKS, 0u));
    assert(audio_output_should_force_idle(AUDIO_OUTPUT_MAX_BUSY_BLOCKS + 1u, 0u));
}

static void test_slow_blocks_force_an_idle_tick_by_elapsed_time(void)
{
    assert(!audio_output_should_force_idle(1u, AUDIO_OUTPUT_MAX_BUSY_US - 1u));
    assert(audio_output_should_force_idle(1u, AUDIO_OUTPUT_MAX_BUSY_US));
    assert(audio_output_should_force_idle(1u, AUDIO_OUTPUT_MAX_BUSY_US + 1u));
}

/* Model an empty IDF DMA completion queue: i2s_channel_write needs one new
 * buffer whenever the current buffer is exhausted. A 256-frame write over
 * 240-frame DMA repeatedly waits for two completions, even with healthy DMA.
 * The board uses AUDIO_OUTPUT_BLOCK_FRAMES for its DMA quantum too. */
static unsigned maximum_dma_completions_per_write(unsigned dma_frames)
{
    unsigned remaining = 0, maximum = 0;
    for (unsigned block = 0; block < 1000; ++block) {
        unsigned pending = AUDIO_OUTPUT_BLOCK_FRAMES, completions = 0;
        while (pending) {
            if (!remaining) { remaining = dma_frames; ++completions; }
            unsigned copied = pending < remaining ? pending : remaining;
            remaining -= copied;
            pending -= copied;
        }
        if (completions > maximum) maximum = completions;
    }
    return maximum;
}

static void test_dma_quantum_does_not_add_a_second_pacing_wait(void)
{
    assert(maximum_dma_completions_per_write(240) == 2);
    assert(maximum_dma_completions_per_write(AUDIO_OUTPUT_BLOCK_FRAMES) == 1);
    for (unsigned rate = 44100; rate <= 48000; rate += 3900) {
        const unsigned mixer_and_scheduling_us = 1500;
        const unsigned old_wait_us = (2u * 240u * 1000000u + rate - 1u) / rate;
        assert(old_wait_us + mixer_and_scheduling_us > audio_output_late_warning_threshold_us(rate));
        assert(audio_output_block_period_us(rate) + mixer_and_scheduling_us < audio_output_late_warning_threshold_us(rate));
    }
}

int main(void)
{
    assert(audio_output_select_sample_rate(0u) == 0u);
    assert(audio_output_select_sample_rate(8000u) == 44100u);
    assert(audio_output_select_sample_rate(22050u) == 44100u);
    assert(audio_output_select_sample_rate(32000u) == 44100u);
    assert(audio_output_select_sample_rate(44100u) == 44100u);
    assert(audio_output_select_sample_rate(48000u) == 48000u);
    assert(audio_output_select_sample_rate(96000u) == 48000u);
    test_block_period_us_uses_precise_ceil_division();
    test_late_warning_threshold_allows_codec_write_pacing_slack();
    test_continuous_output_periodically_forces_an_idle_tick();
    test_slow_blocks_force_an_idle_tick_by_elapsed_time();
    test_dma_quantum_does_not_add_a_second_pacing_wait();
    puts("audio_output_timing tests passed");
    return 0;
}
