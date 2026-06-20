#include "audio_output_timing.h"

#include <assert.h>
#include <stdio.h>

static void test_block_period_ms_uses_ceil_division(void)
{
    assert(audio_output_block_period_ms(48000) == 6u);
    assert(audio_output_block_period_ms(44100) == 6u);
    assert(audio_output_block_period_ms(32000) == 8u);
}

static void test_block_period_ms_rejects_zero_sample_rate(void)
{
    assert(audio_output_block_period_ms(0) == 0u);
}

static void test_remaining_delay_is_disabled_when_codec_write_paces_output(void)
{
    assert(audio_output_remaining_delay_ms(48000, 0) == 0u);
    assert(audio_output_remaining_delay_ms(48000, 5000) == 0u);
    assert(audio_output_remaining_delay_ms(48000, 5334) == 0u);
    assert(audio_output_remaining_delay_ms(48000, 7000) == 0u);
    assert(audio_output_remaining_delay_ms(0, 0) == 0u);
}

int main(void)
{
    test_block_period_ms_uses_ceil_division();
    test_block_period_ms_rejects_zero_sample_rate();
    test_remaining_delay_is_disabled_when_codec_write_paces_output();
    puts("audio_output_timing tests passed");
    return 0;
}
