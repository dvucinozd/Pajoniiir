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
} ui_overview_wave_cache_update_kind_t;

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
    const uint16_t *palette;
    size_t palette_count;
} ui_overview_wave_cache_t;

typedef struct {
    ui_overview_wave_cache_update_kind_t kind;
    int scroll_dx_px;
    uint16_t columns_rendered;
    bool blit_required;
} ui_overview_wave_cache_report_t;

void ui_overview_wave_cache_reset(ui_overview_wave_cache_t *cache);

bool ui_overview_wave_cache_bind(ui_overview_wave_cache_t *cache,
                                 uint16_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px,
                                 const uint16_t *palette,
                                 size_t palette_count);

bool ui_overview_wave_cache_update(ui_overview_wave_cache_t *cache,
                                   const ui_waveform_source_t *source,
                                   uint32_t duration_ms,
                                   const anlz_metadata_t *meta,
                                   uint32_t center_ms,
                                   uint32_t window_ms,
                                   ui_overview_wave_cache_report_t *out_report);

#ifdef __cplusplus
}
#endif
