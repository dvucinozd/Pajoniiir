#pragma once

#include <stdint.h>

#define AUDIO_OUTPUT_BLOCK_FRAMES 256u

uint32_t audio_output_block_period_ms(uint32_t sample_rate);
uint32_t audio_output_remaining_delay_ms(uint32_t sample_rate, uint32_t elapsed_us);
