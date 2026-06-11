#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rekordbox_anlz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_WAVEFORM_SOURCE_NONE = 0,
    UI_WAVEFORM_SOURCE_LOW,
    UI_WAVEFORM_SOURCE_HIGH,
} ui_waveform_source_kind_t;

typedef struct {
    ui_waveform_source_kind_t kind;
    const uint8_t *samples;
    uint32_t sample_count;
} ui_waveform_source_t;

typedef struct {
    uint8_t peak;
    uint8_t palette_index;
} ui_waveform_column_t;

ui_waveform_source_t ui_waveform_source_select(const anlz_metadata_t *meta,
                                               const uint8_t *fallback_low,
                                               bool fallback_low_valid);

uint8_t ui_waveform_palette_for_sample(uint8_t sample);

ui_waveform_column_t ui_waveform_column_for_column(const ui_waveform_source_t *source,
                                                   uint32_t duration_ms,
                                                   int64_t window_start_ms,
                                                   uint32_t window_span_ms,
                                                   int column,
                                                   int column_count);

uint8_t ui_waveform_peak_for_column(const ui_waveform_source_t *source,
                                    uint32_t duration_ms,
                                    int64_t window_start_ms,
                                    uint32_t window_span_ms,
                                    int column,
                                    int column_count);

#ifdef __cplusplus
}
#endif
