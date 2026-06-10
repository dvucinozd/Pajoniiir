#include "ui_mixer_view.h"

static uint8_t pct_from_float(float value)
{
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 100;
    }
    return (uint8_t)(value * 100.0f + 0.5f);
}

static uint8_t pct_from_raw(uint16_t raw)
{
    if (raw >= UI_MIXER_RAW_MAX) {
        return 100;
    }
    return (uint8_t)(((uint32_t)raw * 100u + (UI_MIXER_RAW_MAX / 2u)) / UI_MIXER_RAW_MAX);
}

ui_mixer_deck_view_t ui_mixer_deck_view_from_state(uint16_t raw_fader,
                                                   float output_gain,
                                                   bool pfl_on)
{
    ui_mixer_deck_view_t view = {
        .fader_pct = pct_from_raw(raw_fader),
        .output_pct = pct_from_float(output_gain),
        .pfl_on = pfl_on,
    };
    return view;
}

int ui_mixer_crossfader_knob_x(uint16_t raw_crossfader, int track_width)
{
    if (track_width <= 0) {
        return 0;
    }
    if (raw_crossfader >= UI_MIXER_RAW_MAX) {
        return track_width;
    }
    return (int)(((uint32_t)raw_crossfader * (uint32_t)track_width +
                  (UI_MIXER_RAW_MAX / 2u)) / UI_MIXER_RAW_MAX);
}
