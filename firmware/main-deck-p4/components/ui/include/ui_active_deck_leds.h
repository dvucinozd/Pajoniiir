#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t play;
    uint8_t cue;
    uint8_t beat;
    uint8_t end;
} ui_active_deck_leds_t;

ui_active_deck_leds_t ui_active_deck_leds_calculate(bool playing,
                                                    uint32_t position_ms,
                                                    uint32_t cue_point_ms,
                                                    uint32_t duration_ms,
                                                    bool beat_valid,
                                                    uint16_t beat_progress_permille);

#ifdef __cplusplus
}
#endif
