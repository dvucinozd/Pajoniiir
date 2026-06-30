#include "ui_overview_window.h"

uint32_t ui_overview_window_ms_from_bpm_x100(uint16_t bpm_x100,
                                             uint16_t fallback_bpm)
{
    uint32_t beat_ms = 0;
    if (bpm_x100 > 0) {
        beat_ms = 6000000u / bpm_x100;
    } else if (fallback_bpm > 0) {
        beat_ms = 60000u / fallback_bpm;
    }

    if (beat_ms == 0) {
        beat_ms = UI_OVERVIEW_WINDOW_DEFAULT_BEAT_MS;
    }

    uint32_t window_ms = beat_ms * UI_OVERVIEW_WINDOW_VISIBLE_BEATS;
    if (window_ms < UI_OVERVIEW_WINDOW_MIN_MS) {
        window_ms = UI_OVERVIEW_WINDOW_MIN_MS;
    }
    if (window_ms > UI_OVERVIEW_WINDOW_MAX_MS) {
        window_ms = UI_OVERVIEW_WINDOW_MAX_MS;
    }
    return window_ms;
}
