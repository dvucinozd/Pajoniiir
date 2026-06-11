#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui_waveform_model.h"

static void test_prefers_high_resolution_waveform_when_available(void)
{
    uint8_t high[16] = {0};
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.has_waveform_low = true;
    meta.waveform_low[0] = 31u;
    meta.waveform_high = high;
    meta.waveform_high_len = sizeof(high);

    ui_waveform_source_t source =
        ui_waveform_source_select(&meta, meta.waveform_low, meta.has_waveform_low);

    assert(source.kind == UI_WAVEFORM_SOURCE_HIGH);
    assert(source.samples == high);
    assert(source.sample_count == sizeof(high));
}

static void test_falls_back_to_low_resolution_waveform(void)
{
    uint8_t low[ANLZ_WAVEFORM_LOW_LEN] = {0};
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    low[200] = 17u;

    ui_waveform_source_t source = ui_waveform_source_select(&meta, low, true);
    uint8_t peak = ui_waveform_peak_for_column(&source, 400000u, 0, 400000u, 2, 4);

    assert(source.kind == UI_WAVEFORM_SOURCE_LOW);
    assert(source.sample_count == ANLZ_WAVEFORM_LOW_LEN);
    assert(peak == 17u);
}

static void test_peak_sampling_uses_high_resolution_window(void)
{
    uint8_t high[16] = {0};
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.waveform_high = high;
    meta.waveform_high_len = sizeof(high);
    high[4] = 0x9Fu;  // high bits may carry color; low 5 bits are amplitude 31
    high[5] = 9u;

    ui_waveform_source_t source = ui_waveform_source_select(&meta, NULL, false);
    uint8_t peak = ui_waveform_peak_for_column(&source, 16000u, 0, 16000u, 2, 8);

    assert(source.kind == UI_WAVEFORM_SOURCE_HIGH);
    assert(peak == 31u);
}

static void test_window_outside_track_returns_silence(void)
{
    uint8_t high[16];
    memset(high, 0x1Fu, sizeof(high));
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.waveform_high = high;
    meta.waveform_high_len = sizeof(high);

    ui_waveform_source_t source = ui_waveform_source_select(&meta, NULL, false);
    uint8_t peak = ui_waveform_peak_for_column(&source, 16000u, -8000, 4000u, 0, 4);

    assert(peak == 0u);
}

int main(void)
{
    test_prefers_high_resolution_waveform_when_available();
    test_falls_back_to_low_resolution_waveform();
    test_peak_sampling_uses_high_resolution_window();
    test_window_outside_track_returns_silence();

    puts("ui_waveform_model tests passed");
    return 0;
}
