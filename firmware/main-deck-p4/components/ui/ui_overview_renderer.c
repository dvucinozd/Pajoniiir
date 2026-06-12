#include "ui_overview_renderer.h"

#include <string.h>

#include "ui_overview_grid.h"

static void draw_zoom_grid(uint8_t *pixels,
                           int stride_px,
                           int width_px,
                           int height_px,
                           int64_t window_start_ms,
                           uint32_t window_span_ms,
                           const anlz_metadata_t *meta)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0 ||
        window_span_ms == 0) {
        return;
    }

    if (meta && meta->beats && meta->beat_count > 0) {
        int last_x = -1000;
        for (uint16_t b = 0; b < meta->beat_count; b++) {
            int64_t beat_ms = meta->beats[b].time_ms;
            if (beat_ms < window_start_ms ||
                beat_ms > window_start_ms + (int64_t)window_span_ms) {
                continue;
            }

            int x = (int)(((beat_ms - window_start_ms) * width_px) / window_span_ms);
            if (x < 0 || x >= width_px || x == last_x) {
                continue;
            }
            last_x = x;

            ui_overview_grid_style_t style =
                ui_overview_grid_style_for_phase(meta->beats[b].beat_phase);
            int y0 = (height_px * style.y0_permille) / 1000;
            int y1 = (height_px * style.y1_permille) / 1000;
            if (y1 <= y0) y1 = y0 + 1;
            if (y1 > height_px) y1 = height_px;
            for (int lx = 0; lx < style.line_width_px && x + lx < width_px; lx++) {
                for (int y = y0; y < y1; y++) {
                    pixels[y * stride_px + x + lx] = style.palette_index;
                }
            }
        }
        return;
    }

    ui_overview_grid_style_t style = ui_overview_grid_style_for_phase(1);
    int y0 = (height_px * style.y0_permille) / 1000;
    int y1 = (height_px * style.y1_permille) / 1000;
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > height_px) y1 = height_px;
    for (int x = width_px / 4; x < width_px; x += width_px / 4) {
        for (int y = y0; y < y1; y++) {
            pixels[y * stride_px + x] = style.palette_index;
        }
    }
}

static void draw_center_playhead(uint8_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0) {
        return;
    }

    int center_x = width_px / 2;
    for (int dx = -1; dx <= 1; dx++) {
        int x = center_x + dx;
        if (x < 0 || x >= width_px) {
            continue;
        }
        for (int y = 0; y < height_px; y++) {
            pixels[y * stride_px + x] = 4;
        }
    }
}

void ui_overview_renderer_draw_main(uint8_t *pixels,
                                    int stride_px,
                                    int width_px,
                                    int height_px,
                                    const ui_waveform_source_t *source,
                                    uint32_t duration_ms,
                                    const anlz_metadata_t *meta,
                                    uint32_t center_ms,
                                    uint32_t window_ms)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0) {
        return;
    }

    int64_t window_start_ms = (int64_t)center_ms - ((int64_t)window_ms / 2);

    memset(pixels, 0, (size_t)stride_px * height_px * sizeof(uint8_t));
    draw_zoom_grid(pixels, stride_px, width_px, height_px,
                   window_start_ms, window_ms, meta);

    if (source && source->kind != UI_WAVEFORM_SOURCE_NONE && duration_ms > 0) {
        for (int x = 0; x < width_px; x++) {
            ui_waveform_column_t col = ui_waveform_column_for_column(
                source, duration_ms, window_start_ms, window_ms, x, width_px);
            int amp = col.peak;
            int h = 2 + (amp * (height_px - 8)) / 31;
            if (h < 1) h = 1;
            int cy = height_px / 2;
            int y0 = cy - h / 2;
            int y1 = cy + h / 2;
            if (y0 < 0) y0 = 0;
            if (y1 >= height_px) y1 = height_px - 1;
            uint8_t color = col.palette_index ? col.palette_index : 1;
            for (int y = y0; y <= y1; y++) {
                pixels[y * stride_px + x] = color;
            }
        }
    }

    draw_zoom_grid(pixels, stride_px, width_px, height_px,
                   window_start_ms, window_ms, meta);
    draw_center_playhead(pixels, stride_px, width_px, height_px);
}

bool ui_overview_renderer_draw_mini(uint8_t *pixels,
                                    int stride_px,
                                    int width_px,
                                    int height_px,
                                    const ui_waveform_source_t *source,
                                    uint32_t duration_ms)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0) {
        return false;
    }

    memset(pixels, 0, (size_t)stride_px * height_px * sizeof(uint8_t));

    if (!source || source->kind == UI_WAVEFORM_SOURCE_NONE || duration_ms == 0) {
        return false;
    }

    for (int x = 0; x < width_px; x++) {
        ui_waveform_column_t col = ui_waveform_column_for_column(
            source, duration_ms, 0, duration_ms, x, width_px);
        int amp = col.peak;
        int h = (amp * (height_px - 2)) / 31;
        if (h < 1) h = 1;
        int cy = height_px / 2;
        int y0 = cy - h / 2;
        int y1 = cy + h / 2;
        if (y0 < 0) y0 = 0;
        if (y1 >= height_px) y1 = height_px - 1;
        uint8_t color = col.palette_index ? col.palette_index : 1;
        for (int y = y0; y <= y1; y++) {
            pixels[y * stride_px + x] = color;
        }
    }

    return true;
}
