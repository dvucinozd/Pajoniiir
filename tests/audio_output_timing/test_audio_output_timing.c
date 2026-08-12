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

int main(void)
{
    test_block_period_us_uses_precise_ceil_division();
    test_late_warning_threshold_allows_codec_write_pacing_slack();
    test_continuous_output_periodically_forces_an_idle_tick();
    test_slow_blocks_force_an_idle_tick_by_elapsed_time();
    puts("audio_output_timing tests passed");
    return 0;
}
