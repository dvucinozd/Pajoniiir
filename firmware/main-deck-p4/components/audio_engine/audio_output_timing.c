#include "audio_output_timing.h"

uint32_t audio_output_block_period_ms(uint32_t sample_rate)
{
    if (sample_rate == 0u) return 0u;
    return (AUDIO_OUTPUT_BLOCK_FRAMES * 1000u + sample_rate - 1u) / sample_rate;
}
