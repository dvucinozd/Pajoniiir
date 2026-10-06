#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MIXER_RAW_MAX 16383u

typedef struct {
    uint8_t fader_pct;
    uint8_t output_pct;
    bool pfl_on;
} ui_mixer_deck_view_t;

ui_mixer_deck_view_t ui_mixer_deck_view_from_state(uint16_t raw_fader,
                                                   float output_gain,
                                                   bool pfl_on);
int ui_mixer_crossfader_knob_x(uint16_t raw_crossfader, int track_width);

#ifdef __cplusplus
}
#endif
