#include "ui_overview_motion.h"

static uint32_t abs_delta_u32(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

uint32_t ui_overview_motion_redraw_step_ms(ui_waveform_source_kind_t source_kind,
                                           bool playing)
{
    if (source_kind == UI_WAVEFORM_SOURCE_NONE) {
        return UINT32_MAX;
    }

    return playing ? UI_OVERVIEW_MOTION_PLAYING_STEP_MS
                   : UI_OVERVIEW_MOTION_PAUSED_STEP_MS;
}

bool ui_overview_motion_should_redraw(uint32_t last_center_ms,
                                      uint32_t last_window_ms,
                                      uint32_t center_ms,
                                      uint32_t window_ms,
                                      ui_waveform_source_kind_t source_kind,
                                      bool playing)
{
    if (last_center_ms == UINT32_MAX || last_window_ms != window_ms) {
        return true;
    }

    uint32_t step_ms = ui_overview_motion_redraw_step_ms(source_kind, playing);
    if (step_ms == UINT32_MAX) {
        return false;
    }

    return abs_delta_u32(center_ms, last_center_ms) >= step_ms;
}
