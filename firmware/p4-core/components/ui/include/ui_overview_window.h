#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_OVERVIEW_WINDOW_VISIBLE_BEATS 16u
#define UI_OVERVIEW_ZOOM_STEP_COUNT 5u
#define UI_OVERVIEW_WINDOW_MIN_MS 8000u
#define UI_OVERVIEW_ZOOM_WINDOW_MIN_MS 1000u
#define UI_OVERVIEW_WINDOW_MAX_MS 30000u
#define UI_OVERVIEW_WINDOW_DEFAULT_BEAT_MS 500u

uint8_t ui_overview_zoom_step_default(void);
uint8_t ui_overview_zoom_apply_delta(uint8_t step, int delta);
uint8_t ui_overview_zoom_visible_beats_for_step(uint8_t step);
uint32_t ui_overview_window_ms_from_bpm_x100(uint16_t bpm_x100,
                                             uint16_t fallback_bpm);
uint32_t ui_overview_window_ms_from_bpm_x100_for_zoom(uint16_t bpm_x100,
                                                      uint16_t fallback_bpm,
                                                      uint8_t zoom_step);

#ifdef __cplusplus
}
#endif
