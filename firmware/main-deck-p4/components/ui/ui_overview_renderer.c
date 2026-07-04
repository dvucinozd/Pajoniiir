#include "ui_overview_renderer.h"

#include <string.h>

#include "ui_overview_grid.h"

/* "Punchy" transient tips: columns with peak amplitude at/above this get a
 * 2 px white cap at the top and bottom of the mirrored bar so loud transients
 * pop. Palette index 4 is white in every waveform palette. */
#define WAVE_TIP_AMP_MIN 26
#define WAVE_TIP_PALETTE_INDEX 4
#define WAVE_TIP_MIN_BAR_PX 6

/* Active-loop region highlight on the main (zoom) waveform: columns inside the
 * loop get an amber background (palette index 10) and the loop in/out edges get
 * a bright white marker line (reuses the white tip index). */
#define WAVE_LOOP_BG_PALETTE_INDEX 10
#define WAVE_LOOP_MARK_PALETTE_INDEX WAVE_TIP_PALETTE_INDEX

/* Hot-cue vertical markers: palette indices 11..18 hold cue-slot colours 0..7.
 * Drawn from meta->cues, baked into the strip like the beat grid so they scroll
 * with the waveform without LVGL-over-PPA flicker. */
#define WAVE_CUE_BASE_PALETTE_INDEX 11
#define WAVE_CUE_MAX 8

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
                           const anlz_metadata_t *meta,
                           bool downbeats_only,
                           bool regular_beat_cap_bottom,
                           bool caps_only)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0 ||
        window_span_ms == 0) {
        return;
    }

    if (meta && meta->beats && meta->beat_count > 0) {
        int last_x = -1000;
        for (uint16_t b = 0; b < meta->beat_count; b++) {
            bool downbeat = (meta->beats[b].beat_phase % 4u) == 0u;
            if (downbeats_only && !downbeat) {
                continue;
            }
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
            if (caps_only && style.cap_palette_index == 0) {
                continue;
            }
            int y0 = (height_px * style.y0_permille) / 1000;
            int y1 = (height_px * style.y1_permille) / 1000;
            if (y1 <= y0) y1 = y0 + 1;
            if (y1 > height_px) y1 = height_px;
            for (int lx = 0; lx < style.line_width_px && x + lx < width_px; lx++) {
                if (!caps_only) {
                    for (int y = y0; y < y1; y++) {
                        pixels[y * stride_px + x + lx] = style.palette_index;
                    }
                }
                if (style.cap_palette_index != 0 && style.cap_height_px > 0) {
                    int cap_h = style.cap_height_px;
                    if (cap_h > height_px) cap_h = height_px;
                    int cap_y0 = regular_beat_cap_bottom ? height_px - cap_h : 0;
                    for (int y = cap_y0; y < cap_y0 + cap_h; y++) {
                        pixels[y * stride_px + x + lx] = style.cap_palette_index;
                    }
                }
            }
        }
        return;
    }

    if (downbeats_only) {
        return;
    }

    ui_overview_grid_style_t style = ui_overview_grid_style_for_phase(1);
    if (caps_only && style.cap_palette_index == 0) {
        return;
    }
    int y0 = (height_px * style.y0_permille) / 1000;
    int y1 = (height_px * style.y1_permille) / 1000;
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > height_px) y1 = height_px;
    for (int x = width_px / 4; x < width_px; x += width_px / 4) {
        if (!caps_only) {
            for (int y = y0; y < y1; y++) {
                pixels[y * stride_px + x] = style.palette_index;
            }
        }
        if (style.cap_palette_index != 0 && style.cap_height_px > 0) {
            int cap_h = style.cap_height_px;
            if (cap_h > height_px) cap_h = height_px;
            int cap_y0 = regular_beat_cap_bottom ? height_px - cap_h : 0;
            for (int y = cap_y0; y < cap_y0 + cap_h; y++) {
                pixels[y * stride_px + x] = style.cap_palette_index;
            }
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

static ui_waveform_column_t main_waveform_column_for_display(const ui_waveform_source_t *source,
                                                             uint32_t duration_ms,
                                                             int64_t window_start_ms,
                                                             uint32_t window_ms,
                                                             int column,
                                                             int column_count)
{
    ui_waveform_column_t best = ui_waveform_column_for_column(
        source, duration_ms, window_start_ms, window_ms, column, column_count);

    for (int dx = -1; dx <= 1; dx += 2) {
        int neighbor_x = column + dx;
        if (neighbor_x < 0 || neighbor_x >= column_count) {
            continue;
        }
        ui_waveform_column_t neighbor = ui_waveform_column_for_column(
            source, duration_ms, window_start_ms, window_ms, neighbor_x, column_count);
        if (neighbor.peak > best.peak) {
            best = neighbor;
        }
    }

    return best;
}

static uint16_t rgb565_palette_color(const uint16_t *palette,
                                     size_t palette_count,
                                     uint8_t index)
{
    if (!palette || index >= palette_count) {
        return 0;
    }
    return palette[index];
}

static void draw_zoom_grid_rgb565_column_span(uint16_t *pixels,
                                              int stride_px,
                                              int height_px,
                                              int dest_x_px,
                                              int logical_x_px,
                                              int column_count,
                                              int logical_width_px,
                                              int64_t window_start_ms,
                                              uint32_t window_span_ms,
                                              const anlz_metadata_t *meta,
                                              const uint16_t *palette,
                                              size_t palette_count,
                                              bool downbeats_only,
                                              bool regular_beat_cap_bottom,
                                              bool caps_only)
{
    if (!pixels || stride_px <= 0 || height_px <= 0 || logical_width_px <= 0 ||
        dest_x_px < 0 || logical_x_px < 0 || column_count <= 0 ||
        dest_x_px >= stride_px || logical_x_px >= logical_width_px ||
        window_span_ms == 0) {
        return;
    }
    int logical_end_x = logical_x_px + column_count;
    if (logical_end_x > logical_width_px) {
        logical_end_x = logical_width_px;
    }

    if (meta && meta->beats && meta->beat_count > 0) {
        int64_t range_start_ms =
            window_start_ms +
            (((int64_t)(logical_x_px - 4) * (int64_t)window_span_ms) / logical_width_px);
        int64_t range_end_ms =
            window_start_ms +
            (((int64_t)(logical_end_x + 4) * (int64_t)window_span_ms) / logical_width_px);

        int last_x = -1000;
        for (uint16_t b = 0; b < meta->beat_count; b++) {
            bool downbeat = (meta->beats[b].beat_phase % 4u) == 0u;
            if (downbeats_only && !downbeat) {
                continue;
            }
            int64_t beat_ms = meta->beats[b].time_ms;
            if (beat_ms < range_start_ms) {
                continue;
            }
            if (beat_ms > range_end_ms) {
                break;
            }

            int x = (int)(((beat_ms - window_start_ms) * logical_width_px) / window_span_ms);
            if (x < 0 || x >= logical_width_px || x == last_x) {
                continue;
            }
            last_x = x;

            ui_overview_grid_style_t style =
                ui_overview_grid_style_for_phase(meta->beats[b].beat_phase);
            if (caps_only && style.cap_palette_index == 0) {
                continue;
            }
            int y0 = (height_px * style.y0_permille) / 1000;
            int y1 = (height_px * style.y1_permille) / 1000;
            if (y1 <= y0) y1 = y0 + 1;
            if (y1 > height_px) y1 = height_px;
            uint16_t color = rgb565_palette_color(palette, palette_count,
                                                   style.palette_index);
            uint16_t cap_color = rgb565_palette_color(palette, palette_count,
                                                       style.cap_palette_index);
            for (int lx = 0; lx < style.line_width_px && x + lx < logical_width_px; lx++) {
                int gx = x + lx;
                if (gx < logical_x_px || gx >= logical_end_x) {
                    continue;
                }
                int dest_x = dest_x_px + (gx - logical_x_px);
                if (dest_x < 0 || dest_x >= stride_px) {
                    continue;
                }
                if (!caps_only) {
                    for (int y = y0; y < y1; y++) {
                        pixels[y * stride_px + dest_x] = color;
                    }
                }
                if (style.cap_palette_index != 0 && style.cap_height_px > 0) {
                    int cap_h = style.cap_height_px;
                    if (cap_h > height_px) cap_h = height_px;
                    int cap_y0 = regular_beat_cap_bottom ? height_px - cap_h : 0;
                    for (int y = cap_y0; y < cap_y0 + cap_h; y++) {
                        pixels[y * stride_px + dest_x] = cap_color;
                    }
                }
            }
        }
        return;
    }

    if (downbeats_only) {
        return;
    }

    ui_overview_grid_style_t style = ui_overview_grid_style_for_phase(1);
    if (caps_only && style.cap_palette_index == 0) {
        return;
    }
    int y0 = (height_px * style.y0_permille) / 1000;
    int y1 = (height_px * style.y1_permille) / 1000;
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > height_px) y1 = height_px;
    uint16_t color = rgb565_palette_color(palette, palette_count,
                                           style.palette_index);
    uint16_t cap_color = rgb565_palette_color(palette, palette_count,
                                               style.cap_palette_index);
    int step_x = logical_width_px / 4;
    if (step_x < 1) {
        step_x = 1;
    }
    for (int x = step_x; x < logical_width_px; x += step_x) {
        if (x < logical_x_px || x >= logical_end_x) {
            continue;
        }
        int dest_x = dest_x_px + (x - logical_x_px);
        if (dest_x < 0 || dest_x >= stride_px) {
            continue;
        }
        if (!caps_only) {
            for (int y = y0; y < y1; y++) {
                pixels[y * stride_px + dest_x] = color;
            }
        }
        if (style.cap_palette_index != 0 && style.cap_height_px > 0) {
            int cap_h = style.cap_height_px;
            if (cap_h > height_px) cap_h = height_px;
            int cap_y0 = regular_beat_cap_bottom ? height_px - cap_h : 0;
            for (int y = cap_y0; y < cap_y0 + cap_h; y++) {
                pixels[y * stride_px + dest_x] = cap_color;
            }
        }
    }
}

static void draw_center_playhead_rgb565(uint16_t *pixels,
                                        int stride_px,
                                        int width_px,
                                        int height_px,
                                        const uint16_t *palette,
                                        size_t palette_count)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0) {
        return;
    }

    uint16_t color = rgb565_palette_color(palette, palette_count, 4);
    int center_x = width_px / 2;
    for (int dx = -1; dx <= 1; dx++) {
        int x = center_x + dx;
        if (x < 0 || x >= width_px) {
            continue;
        }
        for (int y = 0; y < height_px; y++) {
            pixels[y * stride_px + x] = color;
        }
    }
}

static void clear_rgb565_columns(uint16_t *pixels,
                                 int stride_px,
                                 int height_px,
                                 int dest_x,
                                 int column_count,
                                 uint16_t background)
{
    for (int y = 0; y < height_px; y++) {
        uint16_t *row = &pixels[y * stride_px + dest_x];
        for (int x = 0; x < column_count; x++) {
            row[x] = background;
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
    ui_overview_renderer_draw_main_with_options(pixels, stride_px, width_px,
                                                height_px, source,
                                                duration_ms, meta, center_ms,
                                                window_ms, false);
}

void ui_overview_renderer_draw_main_with_options(uint8_t *pixels,
                                                 int stride_px,
                                                 int width_px,
                                                 int height_px,
                                                 const ui_waveform_source_t *source,
                                                 uint32_t duration_ms,
                                                 const anlz_metadata_t *meta,
                                                 uint32_t center_ms,
                                                 uint32_t window_ms,
                                                 bool regular_beat_cap_bottom)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0) {
        return;
    }

    int64_t window_start_ms = (int64_t)center_ms - ((int64_t)window_ms / 2);

    memset(pixels, 0, (size_t)stride_px * height_px * sizeof(uint8_t));
    draw_zoom_grid(pixels, stride_px, width_px, height_px,
                   window_start_ms, window_ms, meta, false,
                   regular_beat_cap_bottom, false);

    if (source && source->kind != UI_WAVEFORM_SOURCE_NONE && duration_ms > 0) {
        for (int x = 0; x < width_px; x++) {
            ui_waveform_column_t col = main_waveform_column_for_display(
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
            if (amp >= WAVE_TIP_AMP_MIN && (y1 - y0) >= WAVE_TIP_MIN_BAR_PX) {
                pixels[y0 * stride_px + x] = WAVE_TIP_PALETTE_INDEX;
                pixels[(y0 + 1) * stride_px + x] = WAVE_TIP_PALETTE_INDEX;
                pixels[y1 * stride_px + x] = WAVE_TIP_PALETTE_INDEX;
                pixels[(y1 - 1) * stride_px + x] = WAVE_TIP_PALETTE_INDEX;
            }
        }
    }

    draw_zoom_grid(pixels, stride_px, width_px, height_px,
                   window_start_ms, window_ms, meta, false,
                   regular_beat_cap_bottom, true);
    draw_zoom_grid(pixels, stride_px, width_px, height_px,
                   window_start_ms, window_ms, meta, true,
                   regular_beat_cap_bottom, false);
    draw_center_playhead(pixels, stride_px, width_px, height_px);
}

void ui_overview_renderer_draw_main_rgb565_column_span(uint16_t *pixels,
                                                       int stride_px,
                                                       int height_px,
                                                       int dest_x_px,
                                                       int logical_x_px,
                                                       int column_count,
                                                       int logical_width_px,
                                                       const ui_waveform_source_t *source,
                                                       uint32_t duration_ms,
                                                       const anlz_metadata_t *meta,
                                                       uint32_t center_ms,
                                                       uint32_t window_ms,
                                                       const uint16_t *palette,
                                                       size_t palette_count,
                                                       bool regular_beat_cap_bottom,
                                                       bool loop_active,
                                                       uint32_t loop_start_ms,
                                                       uint32_t loop_end_ms)
{
    if (!pixels || stride_px <= 0 || height_px <= 0 ||
        logical_width_px <= 0 || column_count <= 0 ||
        dest_x_px >= stride_px || logical_x_px >= logical_width_px) {
        return;
    }
    if (dest_x_px < 0) {
        int clipped = -dest_x_px;
        column_count -= clipped;
        logical_x_px += clipped;
        dest_x_px = 0;
    }
    if (logical_x_px < 0) {
        int clipped = -logical_x_px;
        column_count -= clipped;
        dest_x_px += clipped;
        logical_x_px = 0;
    }
    if (column_count <= 0 || dest_x_px >= stride_px ||
        logical_x_px >= logical_width_px) {
        return;
    }
    if (dest_x_px + column_count > stride_px) {
        column_count = stride_px - dest_x_px;
    }
    if (logical_x_px + column_count > logical_width_px) {
        column_count = logical_width_px - logical_x_px;
    }
    if (column_count <= 0) {
        return;
    }

    uint16_t background = rgb565_palette_color(palette, palette_count, 0);
    clear_rgb565_columns(pixels, stride_px, height_px, dest_x_px, column_count,
                         background);

    int64_t window_start_ms = (int64_t)center_ms - ((int64_t)window_ms / 2);

    /* Active-loop background: tint the columns whose time falls inside the loop
     * amber, before the grid/waveform draw over it. Baked into the strip so it
     * scrolls and PPA-blits atomically with the waveform. */
    bool loop_valid = loop_active && loop_end_ms > loop_start_ms;
    if (loop_valid) {
        uint16_t loop_bg = rgb565_palette_color(palette, palette_count,
                                                WAVE_LOOP_BG_PALETTE_INDEX);
        for (int i = 0; i < column_count; i++) {
            int logical_x = logical_x_px + i;
            int64_t col_ms = window_start_ms +
                ((int64_t)logical_x * (int64_t)window_ms) / logical_width_px;
            if (col_ms >= (int64_t)loop_start_ms && col_ms < (int64_t)loop_end_ms) {
                int dest_x = dest_x_px + i;
                for (int y = 0; y < height_px; y++) {
                    pixels[y * stride_px + dest_x] = loop_bg;
                }
            }
        }
    }

    draw_zoom_grid_rgb565_column_span(pixels, stride_px, height_px,
                                      dest_x_px, logical_x_px, column_count,
                                      logical_width_px, window_start_ms,
                                      window_ms, meta, palette, palette_count,
                                      false, regular_beat_cap_bottom, false);

    if (source && source->kind != UI_WAVEFORM_SOURCE_NONE && source->samples &&
        source->sample_count > 0 && duration_ms > 0 && window_ms > 0) {
        for (int i = 0; i < column_count; i++) {
            int logical_x = logical_x_px + i;
            int dest_x = dest_x_px + i;
            ui_waveform_column_t col = main_waveform_column_for_display(
                source, duration_ms, window_start_ms, window_ms, logical_x, logical_width_px);
            int amp = col.peak;
            int h = 2 + (amp * (height_px - 8)) / 31;
            if (h < 1) h = 1;
            int cy = height_px / 2;
            int y0 = cy - h / 2;
            int y1 = cy + h / 2;
            if (y0 < 0) y0 = 0;
            if (y1 >= height_px) y1 = height_px - 1;
            uint8_t palette_index = col.palette_index ? col.palette_index : 1;
            uint16_t color = rgb565_palette_color(palette, palette_count,
                                                   palette_index);
            for (int y = y0; y <= y1; y++) {
                pixels[y * stride_px + dest_x] = color;
            }
            if (amp >= WAVE_TIP_AMP_MIN && (y1 - y0) >= WAVE_TIP_MIN_BAR_PX) {
                uint16_t tip = rgb565_palette_color(palette, palette_count,
                                                    WAVE_TIP_PALETTE_INDEX);
                pixels[y0 * stride_px + dest_x] = tip;
                pixels[(y0 + 1) * stride_px + dest_x] = tip;
                pixels[y1 * stride_px + dest_x] = tip;
                pixels[(y1 - 1) * stride_px + dest_x] = tip;
            }
        }
    }

    draw_zoom_grid_rgb565_column_span(pixels, stride_px, height_px,
                                      dest_x_px, logical_x_px, column_count,
                                      logical_width_px, window_start_ms,
                                      window_ms, meta, palette, palette_count,
                                      false, regular_beat_cap_bottom, true);
    draw_zoom_grid_rgb565_column_span(pixels, stride_px, height_px,
                                      dest_x_px, logical_x_px, column_count,
                                      logical_width_px, window_start_ms,
                                      window_ms, meta, palette, palette_count,
                                      true, regular_beat_cap_bottom, false);

    /* Loop in/out edge markers: bright vertical lines over everything. */
    if (loop_valid) {
        uint16_t mark = rgb565_palette_color(palette, palette_count,
                                             WAVE_LOOP_MARK_PALETTE_INDEX);
        const uint32_t edges[2] = { loop_start_ms, loop_end_ms };
        for (int e = 0; e < 2; e++) {
            int edge_x = (int)(((int64_t)edges[e] - window_start_ms) *
                               (int64_t)logical_width_px / (int64_t)window_ms);
            if (edge_x < logical_x_px || edge_x >= logical_x_px + column_count) {
                continue;
            }
            int dest_x = dest_x_px + (edge_x - logical_x_px);
            for (int y = 0; y < height_px; y++) {
                pixels[y * stride_px + dest_x] = mark;
            }
        }
    }

    /* Hot-cue markers: a full-height line in the cue-slot colour plus a short
     * solid head at the top so the cue reads even over a busy waveform. */
    if (meta && meta->cue_count > 0 && window_ms > 0) {
        int head_h = height_px / 6;
        if (head_h < 3) head_h = 3;
        for (uint8_t c = 0; c < meta->cue_count && c < WAVE_CUE_MAX; c++) {
            uint8_t slot = meta->cues[c].index;
            if (slot >= WAVE_CUE_MAX) {
                continue;
            }
            int cue_x = (int)(((int64_t)meta->cues[c].start_ms - window_start_ms) *
                              (int64_t)logical_width_px / (int64_t)window_ms);
            if (cue_x < logical_x_px || cue_x >= logical_x_px + column_count) {
                continue;
            }
            int dest_x = dest_x_px + (cue_x - logical_x_px);
            uint16_t cue_color = rgb565_palette_color(palette, palette_count,
                                                      WAVE_CUE_BASE_PALETTE_INDEX + slot);
            for (int y = 0; y < height_px; y++) {
                pixels[y * stride_px + dest_x] = cue_color;
            }
            for (int hx = 0; hx < 3; hx++) {
                int head_dx = dest_x + hx;
                if (head_dx >= dest_x_px + column_count || head_dx >= stride_px) {
                    break;
                }
                for (int y = 0; y < head_h; y++) {
                    pixels[y * stride_px + head_dx] = cue_color;
                }
            }
        }
    }
}

void ui_overview_renderer_draw_main_rgb565_columns(uint16_t *pixels,
                                                   int stride_px,
                                                   int width_px,
                                                   int height_px,
                                                   int dest_x,
                                                   int column_count,
                                                   const ui_waveform_source_t *source,
                                                   uint32_t duration_ms,
                                                   const anlz_metadata_t *meta,
                                                   uint32_t center_ms,
                                                   uint32_t window_ms,
                                                   const uint16_t *palette,
                                                   size_t palette_count)
{
    ui_overview_renderer_draw_main_rgb565_column_span(pixels, stride_px,
                                                      height_px, dest_x,
                                                      dest_x, column_count,
                                                      width_px, source,
                                                      duration_ms, meta,
                                                      center_ms, window_ms,
                                                      palette, palette_count,
                                                      false,
                                                      false, 0, 0);
}

void ui_overview_renderer_draw_main_rgb565(uint16_t *pixels,
                                           int stride_px,
                                           int width_px,
                                           int height_px,
                                           const ui_waveform_source_t *source,
                                           uint32_t duration_ms,
                                           const anlz_metadata_t *meta,
                                           uint32_t center_ms,
                                           uint32_t window_ms,
                                           const uint16_t *palette,
                                           size_t palette_count)
{
    ui_overview_renderer_draw_main_rgb565_with_options(pixels, stride_px,
                                                       width_px, height_px,
                                                       source, duration_ms,
                                                       meta, center_ms,
                                                       window_ms, palette,
                                                       palette_count, false);
}

void ui_overview_renderer_draw_main_rgb565_with_options(uint16_t *pixels,
                                                        int stride_px,
                                                        int width_px,
                                                        int height_px,
                                                        const ui_waveform_source_t *source,
                                                        uint32_t duration_ms,
                                                        const anlz_metadata_t *meta,
                                                        uint32_t center_ms,
                                                        uint32_t window_ms,
                                                        const uint16_t *palette,
                                                        size_t palette_count,
                                                        bool regular_beat_cap_bottom)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0) {
        return;
    }

    uint16_t background = rgb565_palette_color(palette, palette_count, 0);
    for (int y = 0; y < height_px; y++) {
        for (int x = 0; x < width_px; x++) {
            pixels[y * stride_px + x] = background;
        }
    }

    ui_overview_renderer_draw_main_rgb565_columns(pixels, stride_px, width_px,
                                                  height_px, 0, width_px,
                                                  source, duration_ms, meta,
                                                  center_ms, window_ms,
                                                  palette, palette_count);
    if (regular_beat_cap_bottom) {
        /*
         * The column helper defaults to the Deck-2/top-cap convention.
         * Re-render through the option-aware span path when Deck 1 needs
         * bottom caps.
         */
        ui_overview_renderer_draw_main_rgb565_column_span(pixels, stride_px,
                                                          height_px, 0, 0,
                                                          width_px, width_px,
                                                          source, duration_ms,
                                                          meta, center_ms,
                                                          window_ms, palette,
                                                          palette_count,
                                                          true,
                                                          false, 0, 0);
    }
    draw_center_playhead_rgb565(pixels, stride_px, width_px, height_px,
                                palette, palette_count);
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
