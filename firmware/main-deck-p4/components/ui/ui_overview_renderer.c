#include "ui_overview_renderer.h"

#include <string.h>

#include "ui_overview_grid.h"

typedef struct {
    uint8_t avg;
    uint8_t peak;
    uint8_t palette_index;
} mini_waveform_column_t;

static bool mini_waveform_column_for_column(const ui_waveform_source_t *source,
                                            uint32_t duration_ms,
                                            int column,
                                            int column_count,
                                            mini_waveform_column_t *out)
{
    if (!out) return false;
    *out = (mini_waveform_column_t){0};
    if (!source || !source->samples || source->sample_count == 0 ||
        duration_ms == 0 || column < 0 || column_count <= 0 ||
        column >= column_count) {
        return false;
    }

    uint64_t sample_start = ((uint64_t)column * source->sample_count) / (uint64_t)column_count;
    uint64_t sample_end = ((uint64_t)(column + 1) * source->sample_count + (uint64_t)column_count - 1u) /
                          (uint64_t)column_count;
    if (sample_start >= source->sample_count) {
        return false;
    }
    if (sample_end > source->sample_count) {
        sample_end = source->sample_count;
    }
    if (sample_end <= sample_start) {
        sample_end = sample_start + 1u;
    }

    uint32_t sum = 0;
    uint32_t count = 0;
    uint8_t peak = 0;
    uint8_t peak_sample = 0;
    for (uint64_t i = sample_start; i < sample_end; i++) {
        uint8_t sample = source->samples[i];
        uint8_t amp = sample & 0x1Fu;
        sum += amp;
        count++;
        if (amp > peak) {
            peak = amp;
            peak_sample = sample;
        }
    }

    out->avg = count ? (uint8_t)((sum + (count / 2u)) / count) : 0;
    out->peak = peak;
    out->palette_index = peak ? ui_waveform_palette_for_sample(peak_sample) : 0;
    return true;
}

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

    int body_span = height_px > 8 ? height_px - 8 : height_px - 2;
    if (body_span < 1) body_span = height_px;
    int peak_span = height_px > 3 ? height_px - 2 : height_px;
    int bottom = height_px - 1;

    int bar_count = (width_px + 1) / 2;
    if (bar_count < 1) bar_count = 1;

    for (int bar = 0; bar < bar_count; bar++) {
        int x = bar * 2;
        if (x >= width_px) {
            break;
        }

        mini_waveform_column_t col;
        if (!mini_waveform_column_for_column(source, duration_ms, bar, bar_count, &col)) {
            continue;
        }

        int body_h = (col.avg * body_span) / 31;
        if (col.avg > 0 && body_h < 1) body_h = 1;
        if (body_h > height_px) body_h = height_px;
        int peak_h = (col.peak * peak_span) / 31;
        if (col.peak > 0 && peak_h < 1) peak_h = 1;
        if (peak_h > height_px) peak_h = height_px;

        uint8_t color = col.palette_index ? col.palette_index : 1;
        int body_top = bottom + 1;
        if (body_h > 0) {
            body_top = bottom - body_h + 1;
            if (body_top < 0) body_top = 0;
            for (int y = body_top; y <= bottom; y++) {
                pixels[y * stride_px + x] = color;
            }
        }

        if (peak_h > body_h + 1) {
            int peak_y = bottom - peak_h + 1;
            if (peak_y < 0) peak_y = 0;
            int stem_bottom = body_top > 0 ? body_top - 1 : 0;
            for (int y = peak_y; y <= stem_bottom; y++) {
                pixels[y * stride_px + x] = color;
            }
        }
    }

    return true;
}
