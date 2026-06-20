#include "audio_output_timing.h"

uint32_t audio_output_block_period_ms(uint32_t sample_rate)
{
    if (sample_rate == 0u) return 0u;
    return (AUDIO_OUTPUT_BLOCK_FRAMES * 1000u + sample_rate - 1u) / sample_rate;
}

uint32_t audio_output_remaining_delay_ms(uint32_t sample_rate, uint32_t elapsed_us)
{
    if (sample_rate == 0u) return 0u;
    uint32_t period_us = (AUDIO_OUTPUT_BLOCK_FRAMES * 1000000u + sample_rate - 1u) / sample_rate;
    if (elapsed_us >= period_us) return 0u;
    uint32_t remaining_us = period_us - elapsed_us;
    return (remaining_us + 999u) / 1000u;
}
