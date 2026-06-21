#pragma once

#include <stdint.h>
#include "rekordbox_anlz.h"

uint32_t beat_jump_calculate_target_ms(uint32_t position_ms,
                                       uint16_t bpm,
                                       int beat_shift,
                                       const anlz_metadata_t *meta);
