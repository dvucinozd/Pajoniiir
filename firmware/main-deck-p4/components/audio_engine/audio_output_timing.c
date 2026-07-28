#include "audio_output_timing.h"

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

bool audio_output_should_force_idle(uint32_t consecutive_busy_blocks)
{
    return consecutive_busy_blocks >= AUDIO_OUTPUT_MAX_BUSY_BLOCKS;
}
