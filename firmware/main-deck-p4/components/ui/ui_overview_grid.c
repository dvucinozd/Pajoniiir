#include "ui_overview_grid.h"

size_t ui_overview_grid_build_columns(const anlz_beat_t *beats,
                                      uint16_t beat_count,
                                      uint32_t duration_ms,
                                      int width_px,
                                      int min_spacing_px,
                                      int *out_columns,
                                      size_t out_capacity)
{
    if (!beats || beat_count == 0 || duration_ms == 0 ||
        width_px <= 0 || !out_columns || out_capacity == 0) {
        return 0;
    }

    if (min_spacing_px < 1) {
        min_spacing_px = 1;
    }

    size_t count = 0;
    int last_x = -min_spacing_px;

    for (uint16_t b = 0; b < beat_count; b++) {
        if (beats[b].beat_phase != 0) {
            continue;
        }

        int x = (int)(((uint64_t)beats[b].time_ms * (uint32_t)width_px) / duration_ms);
        if (x < 0 || x >= width_px) {
            continue;
        }

        if (count > 0 && (x - last_x) < min_spacing_px) {
            continue;
        }

        out_columns[count++] = x;
        last_x = x;

        if (count == out_capacity) {
            break;
        }
    }

    return count;
}

ui_overview_grid_style_t ui_overview_grid_style_for_phase(uint16_t beat_phase)
{
    if ((beat_phase % 4u) == 0u) {
        return (ui_overview_grid_style_t){
            .palette_index = 4,
            .line_width_px = 2,
            .y0_permille = 0,
            .y1_permille = 1000,
        };
    }

    return (ui_overview_grid_style_t){
        .palette_index = 3,
        .line_width_px = 1,
        .y0_permille = 0,
        .y1_permille = 1000,
    };
}
