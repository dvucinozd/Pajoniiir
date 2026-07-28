#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_OUTPUT_BLOCK_FRAMES 256u
#define AUDIO_OUTPUT_MAX_BUSY_BLOCKS 64u

uint32_t audio_output_block_period_us(uint32_t sample_rate);
uint32_t audio_output_late_warning_threshold_us(uint32_t sample_rate);
bool audio_output_should_force_idle(uint32_t consecutive_busy_blocks);
