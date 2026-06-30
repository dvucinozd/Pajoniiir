#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_OVERVIEW_WINDOW_VISIBLE_BEATS 16u
#define UI_OVERVIEW_WINDOW_MIN_MS 8000u
#define UI_OVERVIEW_WINDOW_MAX_MS 30000u
#define UI_OVERVIEW_WINDOW_DEFAULT_BEAT_MS 500u

uint32_t ui_overview_window_ms_from_bpm_x100(uint16_t bpm_x100,
                                             uint16_t fallback_bpm);

#ifdef __cplusplus
}
#endif
