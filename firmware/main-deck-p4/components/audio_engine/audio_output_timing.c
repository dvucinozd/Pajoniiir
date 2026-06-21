#include "audio_output_timing.h"

uint32_t audio_output_block_period_ms(uint32_t sample_rate)
{
    if (sample_rate == 0u) return 0u;
    return (AUDIO_OUTPUT_BLOCK_FRAMES * 1000u + sample_rate - 1u) / sample_rate;
}

uint32_t audio_output_block_period_us(uint32_t sample_rate)
{
    if (sample_rate == 0u) return 0u;
    return (AUDIO_OUTPUT_BLOCK_FRAMES * 1000000u + sample_rate - 1u) / sample_rate;
}

uint32_t audio_output_late_warning_threshold_us(uint32_t sample_rate)
{
    uint32_t period_us = audio_output_block_period_us(sample_rate);
    return period_us > 0u ? period_us * 2u : 0u;
}

uint32_t audio_output_remaining_delay_ms(uint32_t sample_rate, uint32_t elapsed_us)
{
    (void)sample_rate;
    (void)elapsed_us;
    return 0u;
}
