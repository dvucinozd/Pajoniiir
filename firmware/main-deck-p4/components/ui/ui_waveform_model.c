#include "ui_waveform_model.h"

static ui_waveform_source_t no_source(void)
{
    return (ui_waveform_source_t){0};
}

ui_waveform_source_t ui_waveform_source_select(const anlz_metadata_t *meta,
                                               const uint8_t *fallback_low,
                                               bool fallback_low_valid)
{
    if (meta && meta->waveform_high && meta->waveform_high_len > 0) {
        return (ui_waveform_source_t){
            .kind = UI_WAVEFORM_SOURCE_HIGH,
            .samples = meta->waveform_high,
            .sample_count = meta->waveform_high_len,
        };
    }

    if (meta && meta->has_waveform_low) {
        return (ui_waveform_source_t){
            .kind = UI_WAVEFORM_SOURCE_LOW,
            .samples = meta->waveform_low,
            .sample_count = ANLZ_WAVEFORM_LOW_LEN,
        };
    }

    if (fallback_low && fallback_low_valid) {
        return (ui_waveform_source_t){
            .kind = UI_WAVEFORM_SOURCE_LOW,
            .samples = fallback_low,
            .sample_count = ANLZ_WAVEFORM_LOW_LEN,
        };
    }

    return no_source();
}

uint8_t ui_waveform_palette_for_sample(uint8_t sample)
{
    uint8_t hint = sample >> 5;
    uint8_t amp = sample & 0x1Fu;

    if (hint > 0) {
        static const uint8_t hint_palette[8] = {
            1, 2, 3, 5, 6, 7, 3, 1
        };
        return hint_palette[hint & 0x07u];
    }

    if (amp >= 28u) {
        return 3;  // cyan transient
    }
    if (amp >= 22u) {
        return 1;  // hot pink/red body
    }
    if (amp >= 14u) {
        return 2;  // blue/purple mid energy
    }
    return 7;      // quiet detail
}

ui_waveform_column_t ui_waveform_column_for_column(const ui_waveform_source_t *source,
                                                   uint32_t duration_ms,
                                                   int64_t window_start_ms,
                                                   uint32_t window_span_ms,
                                                   int column,
                                                   int column_count)
{
    if (!source || !source->samples || source->sample_count == 0 ||
        duration_ms == 0 || window_span_ms == 0 ||
        column < 0 || column_count <= 0 || column >= column_count) {
        return (ui_waveform_column_t){0};
    }

    int64_t col_start = window_start_ms +
        ((int64_t)window_span_ms * (int64_t)column) / column_count;
    int64_t col_end = window_start_ms +
        ((int64_t)window_span_ms * (int64_t)(column + 1)) / column_count;

    if (col_end <= 0 || col_start >= (int64_t)duration_ms || col_end <= col_start) {
        return (ui_waveform_column_t){0};
    }

    if (col_start < 0) {
        col_start = 0;
    }
    if (col_end > (int64_t)duration_ms) {
        col_end = duration_ms;
    }
    if (col_end <= col_start) {
        return (ui_waveform_column_t){0};
    }

    uint64_t sample_start = ((uint64_t)col_start * source->sample_count) / duration_ms;
    uint64_t sample_end = (((uint64_t)col_end * source->sample_count) + duration_ms - 1u) / duration_ms;

    if (sample_start >= source->sample_count) {
        return (ui_waveform_column_t){0};
    }
    if (sample_end > source->sample_count) {
        sample_end = source->sample_count;
    }
    if (sample_end <= sample_start) {
        sample_end = sample_start + 1u;
    }

    uint8_t peak = 0;
    uint8_t peak_sample = 0;
    for (uint64_t i = sample_start; i < sample_end; i++) {
        uint8_t amp = source->samples[i] & 0x1Fu;
        if (amp > peak) {
            peak = amp;
            peak_sample = source->samples[i];
        }
    }

    return (ui_waveform_column_t){
        .peak = peak,
        .palette_index = peak ? ui_waveform_palette_for_sample(peak_sample) : 0,
    };
}

uint8_t ui_waveform_peak_for_column(const ui_waveform_source_t *source,
                                    uint32_t duration_ms,
                                    int64_t window_start_ms,
                                    uint32_t window_span_ms,
                                    int column,
                                    int column_count)
{
    return ui_waveform_column_for_column(source, duration_ms, window_start_ms,
                                         window_span_ms, column, column_count).peak;
}
