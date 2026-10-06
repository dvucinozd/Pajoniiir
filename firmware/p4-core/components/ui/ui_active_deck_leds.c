#include "ui_active_deck_leds.h"

ui_active_deck_leds_t ui_active_deck_leds_calculate(bool playing,
                                                    uint32_t position_ms,
                                                    uint32_t cue_point_ms,
                                                    uint32_t duration_ms,
                                                    bool beat_valid,
                                                    uint16_t beat_progress_permille)
{
    ui_active_deck_leds_t leds = {0};
    leds.play = playing ? 1u : 0u;
    leds.cue = (position_ms == cue_point_ms) ? 1u : 0u;
    leds.beat = (playing && beat_valid && beat_progress_permille < 200u) ? 1u : 0u;
    leds.end = (duration_ms > 0u && position_ms < duration_ms &&
                duration_ms - position_ms <= 10000u) ? 1u : 0u;
    return leds;
}
