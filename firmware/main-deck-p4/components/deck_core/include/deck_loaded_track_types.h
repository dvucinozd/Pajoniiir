#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation;
    uint32_t media_generation;
    uint32_t track_key;
    uint32_t duration_ms;
    uint32_t bpm_x100;
    uint16_t bpm;
    uint8_t deck;
    bool valid;
    bool has_anlz;
} deck_loaded_track_summary_t;
