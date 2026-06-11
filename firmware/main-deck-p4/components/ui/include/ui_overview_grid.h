#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rekordbox_anlz.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_OVERVIEW_GRID_DEFAULT_MIN_SPACING_PX 48

typedef struct {
    uint8_t palette_index;
    int line_width_px;
    int y0_permille;
    int y1_permille;
} ui_overview_grid_style_t;

size_t ui_overview_grid_build_columns(const anlz_beat_t *beats,
                                      uint16_t beat_count,
                                      uint32_t duration_ms,
                                      int width_px,
                                      int min_spacing_px,
                                      int *out_columns,
                                      size_t out_capacity);

ui_overview_grid_style_t ui_overview_grid_style_for_phase(uint16_t beat_phase);

#ifdef __cplusplus
}
#endif
