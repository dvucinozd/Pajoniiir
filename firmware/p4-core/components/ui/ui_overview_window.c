#include "ui_overview_window.h"

static const uint8_t s_zoom_visible_beats[UI_OVERVIEW_ZOOM_STEP_COUNT] = {
    4u, 8u, 12u, 16u, 24u
};

uint8_t ui_overview_zoom_step_default(void)
{
    return 3u;
}

uint8_t ui_overview_zoom_apply_delta(uint8_t step, int delta)
{
    int next = (int)step + delta;
    if (next < 0) {
        return 0u;
    }
    if (next >= (int)UI_OVERVIEW_ZOOM_STEP_COUNT) {
        return (uint8_t)(UI_OVERVIEW_ZOOM_STEP_COUNT - 1u);
    }
    return (uint8_t)next;
}

uint8_t ui_overview_zoom_visible_beats_for_step(uint8_t step)
{
    if (step >= UI_OVERVIEW_ZOOM_STEP_COUNT) {
        step = ui_overview_zoom_step_default();
    }
    return s_zoom_visible_beats[step];
}

uint32_t ui_overview_window_ms_from_bpm_x100_for_zoom(uint16_t bpm_x100,
                                                      uint16_t fallback_bpm,
                                                      uint8_t zoom_step)
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

    uint32_t window_ms = beat_ms * ui_overview_zoom_visible_beats_for_step(zoom_step);
    if (window_ms < UI_OVERVIEW_ZOOM_WINDOW_MIN_MS) {
        window_ms = UI_OVERVIEW_ZOOM_WINDOW_MIN_MS;
    }
    if (window_ms > UI_OVERVIEW_WINDOW_MAX_MS) {
        window_ms = UI_OVERVIEW_WINDOW_MAX_MS;
    }
    return window_ms;
}

uint32_t ui_overview_window_ms_from_bpm_x100(uint16_t bpm_x100,
                                             uint16_t fallback_bpm)
{
    uint32_t window_ms = ui_overview_window_ms_from_bpm_x100_for_zoom(
        bpm_x100,
        fallback_bpm,
        ui_overview_zoom_step_default());
    if (window_ms < UI_OVERVIEW_WINDOW_MIN_MS) {
        window_ms = UI_OVERVIEW_WINDOW_MIN_MS;
    }
    return window_ms;
}
