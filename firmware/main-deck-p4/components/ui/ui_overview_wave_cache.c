#include "ui_overview_wave_cache.h"

#include <stdlib.h>
#include <string.h>

#include "ui_overview_renderer.h"

static void report_reset(ui_overview_wave_cache_report_t *report)
{
    if (!report) {
        return;
    }
    *report = (ui_overview_wave_cache_report_t){
        .kind = UI_OVERVIEW_WAVE_CACHE_NONE,
        .scroll_dx_px = 0,
        .columns_rendered = 0,
        .blit_required = false,
    };
}

void ui_overview_wave_cache_reset(ui_overview_wave_cache_t *cache)
{
    if (!cache) {
        return;
    }

    uint16_t *pixels = cache->pixels;
    int stride_px = cache->stride_px;
    int width_px = cache->width_px;
    int height_px = cache->height_px;
    const uint16_t *palette = cache->palette;
    size_t palette_count = cache->palette_count;

    memset(cache, 0, sizeof(*cache));
    cache->pixels = pixels;
    cache->stride_px = stride_px;
    cache->width_px = width_px;
    cache->height_px = height_px;
    cache->palette = palette;
    cache->palette_count = palette_count;
}

bool ui_overview_wave_cache_bind(ui_overview_wave_cache_t *cache,
                                 uint16_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px,
                                 const uint16_t *palette,
                                 size_t palette_count)
{
    if (!cache || !pixels || stride_px < width_px || width_px <= 0 ||
        height_px <= 0 || !palette || palette_count == 0) {
        return false;
    }

    memset(cache, 0, sizeof(*cache));
    cache->pixels = pixels;
    cache->stride_px = stride_px;
    cache->width_px = width_px;
    cache->height_px = height_px;
    cache->palette = palette;
    cache->palette_count = palette_count;
    return true;
}

static bool source_matches(const ui_overview_wave_cache_t *cache,
                           const ui_waveform_source_t *source,
                           uint32_t duration_ms,
                           const anlz_metadata_t *meta,
                           uint32_t window_ms)
{
    return cache && cache->valid && source &&
           cache->source_samples == source->samples &&
           cache->source_sample_count == source->sample_count &&
           cache->source_kind == source->kind &&
           cache->duration_ms == duration_ms &&
           cache->meta == meta &&
           cache->window_ms == window_ms;
}

static void cache_store_key(ui_overview_wave_cache_t *cache,
                            const ui_waveform_source_t *source,
                            uint32_t duration_ms,
                            const anlz_metadata_t *meta,
                            uint32_t center_ms,
                            uint32_t window_ms)
{
    cache->source_samples = source->samples;
    cache->source_sample_count = source->sample_count;
    cache->source_kind = source->kind;
    cache->duration_ms = duration_ms;
    cache->meta = meta;
    cache->center_ms = center_ms;
    cache->window_ms = window_ms;
    cache->valid = true;
}

static int center_delta_to_pixels(uint32_t old_center_ms,
                                  uint32_t new_center_ms,
                                  uint32_t window_ms,
                                  int width_px)
{
    int64_t delta_ms = (int64_t)new_center_ms - (int64_t)old_center_ms;
    int64_t numerator = delta_ms * (int64_t)width_px;
    if (numerator >= 0) {
        return (int)((numerator + (int64_t)(window_ms / 2u)) /
                     (int64_t)window_ms);
    }
    return (int)((numerator - (int64_t)(window_ms / 2u)) /
                 (int64_t)window_ms);
}

static void scroll_pixels(uint16_t *pixels,
                          int stride_px,
                          int width_px,
                          int height_px,
                          int dx)
{
    if (dx == 0) {
        return;
    }

    for (int y = 0; y < height_px; y++) {
        uint16_t *row = &pixels[y * stride_px];
        if (dx > 0) {
            memmove(row, row + dx, (size_t)(width_px - dx) * sizeof(uint16_t));
            memset(row + width_px - dx, 0, (size_t)dx * sizeof(uint16_t));
        } else {
            int left = -dx;
            memmove(row + left, row, (size_t)(width_px - left) * sizeof(uint16_t));
            memset(row, 0, (size_t)left * sizeof(uint16_t));
        }
    }
}

static void redraw_full(ui_overview_wave_cache_t *cache,
                        const ui_waveform_source_t *source,
                        uint32_t duration_ms,
                        const anlz_metadata_t *meta,
                        uint32_t center_ms,
                        uint32_t window_ms)
{
    ui_overview_renderer_draw_main_rgb565_columns(cache->pixels,
                                                  cache->stride_px,
                                                  cache->width_px,
                                                  cache->height_px,
                                                  0,
                                                  cache->width_px,
                                                  source,
                                                  duration_ms,
                                                  meta,
                                                  center_ms,
                                                  window_ms,
                                                  cache->palette,
                                                  cache->palette_count);
}

bool ui_overview_wave_cache_update(ui_overview_wave_cache_t *cache,
                                   const ui_waveform_source_t *source,
                                   uint32_t duration_ms,
                                   const anlz_metadata_t *meta,
                                   uint32_t center_ms,
                                   uint32_t window_ms,
                                   ui_overview_wave_cache_report_t *out_report)
{
    report_reset(out_report);
    if (!cache || !cache->pixels || cache->stride_px < cache->width_px ||
        cache->width_px <= 0 || cache->height_px <= 0 || !cache->palette ||
        cache->palette_count == 0 || !source || !source->samples ||
        source->sample_count == 0 || source->kind == UI_WAVEFORM_SOURCE_NONE ||
        duration_ms == 0 || window_ms == 0) {
        return false;
    }

    bool full = !source_matches(cache, source, duration_ms, meta, window_ms);
    int dx = 0;
    if (!full) {
        dx = center_delta_to_pixels(cache->center_ms, center_ms,
                                    window_ms, cache->width_px);
        if (dx == 0) {
            return false;
        }
        if (abs(dx) >= cache->width_px) {
            full = true;
        }
    }

    ui_overview_wave_cache_report_t report = {
        .kind = UI_OVERVIEW_WAVE_CACHE_NONE,
        .scroll_dx_px = 0,
        .columns_rendered = 0,
        .blit_required = false,
    };

    if (full) {
        redraw_full(cache, source, duration_ms, meta, center_ms, window_ms);
        report.kind = UI_OVERVIEW_WAVE_CACHE_FULL;
        report.columns_rendered = (uint16_t)cache->width_px;
        report.blit_required = true;
    } else {
        scroll_pixels(cache->pixels, cache->stride_px,
                      cache->width_px, cache->height_px, dx);
        if (dx > 0) {
            ui_overview_renderer_draw_main_rgb565_columns(cache->pixels,
                                                          cache->stride_px,
                                                          cache->width_px,
                                                          cache->height_px,
                                                          cache->width_px - dx,
                                                          dx,
                                                          source,
                                                          duration_ms,
                                                          meta,
                                                          center_ms,
                                                          window_ms,
                                                          cache->palette,
                                                          cache->palette_count);
            report.columns_rendered = (uint16_t)dx;
        } else {
            ui_overview_renderer_draw_main_rgb565_columns(cache->pixels,
                                                          cache->stride_px,
                                                          cache->width_px,
                                                          cache->height_px,
                                                          0,
                                                          -dx,
                                                          source,
                                                          duration_ms,
                                                          meta,
                                                          center_ms,
                                                          window_ms,
                                                          cache->palette,
                                                          cache->palette_count);
            report.columns_rendered = (uint16_t)(-dx);
        }
        report.kind = UI_OVERVIEW_WAVE_CACHE_SCROLL;
        report.scroll_dx_px = dx;
        report.blit_required = true;
    }

    cache_store_key(cache, source, duration_ms, meta, center_ms, window_ms);
    if (out_report) {
        *out_report = report;
    }
    return true;
}
