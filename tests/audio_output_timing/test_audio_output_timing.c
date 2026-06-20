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

int main(void)
{
    test_block_period_ms_uses_ceil_division();
    test_block_period_ms_rejects_zero_sample_rate();
    puts("audio_output_timing tests passed");
    return 0;
}
