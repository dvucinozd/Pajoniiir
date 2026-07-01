#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rekordbox_anlz.h"
#include "ui_waveform_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_OVERVIEW_WAVE_CACHE_NONE = 0,
    UI_OVERVIEW_WAVE_CACHE_FULL,
    UI_OVERVIEW_WAVE_CACHE_SCROLL,
    UI_OVERVIEW_WAVE_CACHE_OFFSET,
    UI_OVERVIEW_WAVE_CACHE_EDGE,
    UI_OVERVIEW_WAVE_CACHE_KIND_COUNT,
} ui_overview_wave_cache_update_kind_t;

#define UI_OVERVIEW_WAVE_CACHE_MAX_BLITS 2
#define UI_OVERVIEW_WAVE_CACHE_MARGIN_PX 128
#define UI_OVERVIEW_WAVE_CACHE_EDGE_BATCH_PX 32

typedef struct {
    uint16_t src_x_px;
    uint16_t dst_x_px;
    uint16_t width_px;
} ui_overview_wave_cache_blit_t;

typedef struct {
    bool valid;
    const uint8_t *source_samples;
    uint32_t source_sample_count;
    ui_waveform_source_kind_t source_kind;
    uint32_t duration_ms;
    uint32_t center_ms;
    uint32_t window_ms;
    const anlz_metadata_t *meta;
    uint16_t *pixels;
    int stride_px;
    int width_px;
    int height_px;
    int strip_width_px;
    int view_width_px;
    int margin_px;
    int ring_head_px;
    int view_origin_px;
    int64_t strip_start_ms_q16;
    int64_t ms_per_px_q16;
    uint32_t source_generation;
    const uint16_t *palette;
    size_t palette_count;
    bool regular_beat_cap_bottom;
    struct {
        uint32_t update_count[UI_OVERVIEW_WAVE_CACHE_KIND_COUNT];
        uint32_t total_columns_rendered;
        uint32_t total_blits;
    } stats;
} ui_overview_wave_cache_t;

typedef struct {
    ui_overview_wave_cache_update_kind_t kind;
    int scroll_dx_px;
    uint16_t columns_rendered;
    bool blit_required;
    uint8_t blit_count;
    uint16_t blit_height_px;
    ui_overview_wave_cache_blit_t blit[UI_OVERVIEW_WAVE_CACHE_MAX_BLITS];
} ui_overview_wave_cache_report_t;

typedef struct {
    uint32_t update_count[UI_OVERVIEW_WAVE_CACHE_KIND_COUNT];
    uint32_t total_columns_rendered;
    uint32_t total_blits;
} ui_overview_wave_cache_stats_t;

void ui_overview_wave_cache_reset(ui_overview_wave_cache_t *cache);
void ui_overview_wave_cache_reset_stats(ui_overview_wave_cache_t *cache);
void ui_overview_wave_cache_get_stats(const ui_overview_wave_cache_t *cache,
                                      ui_overview_wave_cache_stats_t *out_stats);

bool ui_overview_wave_cache_bind(ui_overview_wave_cache_t *cache,
                                 uint16_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px,
                                 const uint16_t *palette,
                                 size_t palette_count);

bool ui_overview_wave_cache_bind_strip(ui_overview_wave_cache_t *cache,
                                       uint16_t *pixels,
                                       int stride_px,
                                       int strip_width_px,
                                       int view_width_px,
                                       int height_px,
                                       int margin_px,
                                       const uint16_t *palette,
                                       size_t palette_count);

void ui_overview_wave_cache_set_regular_beat_cap_bottom(ui_overview_wave_cache_t *cache,
                                                        bool enabled);

bool ui_overview_wave_cache_update(ui_overview_wave_cache_t *cache,
                                   const ui_waveform_source_t *source,
                                   uint32_t duration_ms,
                                   const anlz_metadata_t *meta,
                                   uint32_t center_ms,
                                   uint32_t window_ms,
                                   ui_overview_wave_cache_report_t *out_report);

#ifdef UI_OVERVIEW_WAVE_CACHE_TESTING
void ui_overview_wave_cache_test_force_view_origin(ui_overview_wave_cache_t *cache, int origin_px);
#endif

#ifdef __cplusplus
}
#endif
