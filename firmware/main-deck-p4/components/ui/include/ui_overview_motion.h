#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_waveform_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_OVERVIEW_MOTION_PLAYING_STEP_MS 33u
#define UI_OVERVIEW_MOTION_PAUSED_STEP_MS 80u

uint32_t ui_overview_motion_redraw_step_ms(ui_waveform_source_kind_t source_kind,
                                           bool playing);

uint32_t ui_overview_motion_snap_center_ms(uint32_t center_ms,
                                           uint32_t window_ms,
                                           int width_px);

bool ui_overview_motion_should_redraw(uint32_t last_center_ms,
                                      uint32_t last_window_ms,
                                      uint32_t center_ms,
                                      uint32_t window_ms,
                                      ui_waveform_source_kind_t source_kind,
                                      bool playing);

#ifdef __cplusplus
}
#endif
