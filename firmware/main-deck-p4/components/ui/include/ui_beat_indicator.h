#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rekordbox_anlz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;
    uint8_t  phase;              // 0..3, maps to the four visible beat dots
    bool     downbeat;
    uint16_t progress_permille;  // 0 at beat start, 1000 at next beat
} ui_beat_indicator_state_t;

ui_beat_indicator_state_t ui_beat_indicator_calculate(uint32_t position_ms,
                                                       const anlz_beat_t *beats,
                                                       uint16_t beat_count,
                                                       uint16_t fallback_bpm);

#ifdef __cplusplus
}
#endif
