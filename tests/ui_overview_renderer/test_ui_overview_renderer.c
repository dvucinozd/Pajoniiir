#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui_overview_renderer.h"

static void test_main_renderer_clears_and_draws_waveform_columns(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[8 * 10];
    memset(pixels, 9, sizeof(pixels));

    ui_overview_renderer_draw_main(pixels, 8, 8, 10, &source, 8000, NULL, 4000, 8000);

    for (int x = 0; x < 8; x++) {
        assert(pixels[(10 / 2) * 8 + x] != 0);
    }
}

static void test_main_renderer_keeps_downbeat_grid_on_top(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    anlz_beat_t beats[] = {
        {.beat_phase = 0, .bpm_x100 = 12000, .time_ms = 4000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 1,
    };
    uint8_t pixels[8 * 10] = {0};

    ui_overview_renderer_draw_main(pixels, 8, 8, 10, &source, 8000, &meta, 4000, 8000);

    assert(pixels[0 * 8 + 4] == 4);
    assert(pixels[9 * 8 + 4] == 4);
}

static void test_mini_renderer_clears_and_draws_full_track_waveform(void)
{
    uint8_t samples[8] = {0x00, 0x05, 0x0A, 0x1F, 0x1F, 0x0A, 0x05, 0x00};
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[8 * 6];
    memset(pixels, 7, sizeof(pixels));

    bool rendered = ui_overview_renderer_draw_mini(pixels, 8, 8, 6, &source, 8000);

    assert(rendered);
    assert(pixels[(6 / 2) * 8 + 3] != 0);
    assert(pixels[(6 / 2) * 8 + 4] != 0);
}

int main(void)
{
    test_main_renderer_clears_and_draws_waveform_columns();
    test_main_renderer_keeps_downbeat_grid_on_top();
    test_mini_renderer_clears_and_draws_full_track_waveform();

    puts("ui_overview_renderer tests passed");
    return 0;
}
