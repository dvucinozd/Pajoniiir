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

static void test_main_renderer_draws_center_playhead(void)
{
    uint8_t samples[16];
    memset(samples, 0x04u, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[9 * 10] = {0};

    ui_overview_renderer_draw_main(pixels, 9, 9, 10, &source, 8000, NULL, 4000, 8000);

    assert(pixels[0 * 9 + 4] == 4);
    assert(pixels[9 * 9 - 5] == 4);
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
    assert(pixels[(6 - 1) * 8 + 2] != 0);
    assert(pixels[(6 - 1) * 8 + 4] != 0);
}

static int column_height(const uint8_t *pixels, int stride, int height, int x)
{
    int count = 0;
    for (int y = 0; y < height; y++) {
        if (pixels[y * stride + x] != 0) count++;
    }
    return count;
}

static void test_mini_renderer_preserves_spikes_on_tall_canvas(void)
{
    uint8_t samples[8] = {0x00, 0x08, 0x1F, 0x1F, 0x0A, 0x0A, 0x08, 0x00};
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[8 * 45] = {0};

    bool rendered = ui_overview_renderer_draw_mini(pixels, 8, 8, 45, &source, 8000);

    assert(rendered);
    int quiet_h = column_height(pixels, 8, 45, 0);
    int peak_h = column_height(pixels, 8, 45, 2);
    assert(quiet_h < peak_h / 2);
    assert(peak_h > 35);
}

static void test_mini_renderer_does_not_clip_hot_spikes(void)
{
    uint8_t samples[8] = {0x19, 0x19, 0x1F, 0x1F, 0x00, 0x00, 0x19, 0x00};
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[8 * 45] = {0};

    bool rendered = ui_overview_renderer_draw_mini(pixels, 8, 8, 45, &source, 8000);

    assert(rendered);
    int hot_h = column_height(pixels, 8, 45, 0);
    int peak_h = column_height(pixels, 8, 45, 2);
    assert(hot_h < peak_h);
}

static void test_mini_renderer_anchors_body_to_bottom(void)
{
    uint8_t samples[8] = {0x00, 0x08, 0x1F, 0x1F, 0x0A, 0x0A, 0x08, 0x00};
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[8 * 45] = {0};

    bool rendered = ui_overview_renderer_draw_mini(pixels, 8, 8, 45, &source, 8000);

    assert(rendered);
    assert(pixels[(45 - 1) * 8 + 2] != 0);
}

static void test_mini_renderer_connects_peak_stem_to_body(void)
{
    uint8_t samples[8] = {0x1F, 0x00, 0x08, 0x1F, 0x1F, 0x08, 0x00, 0x00};
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[4 * 45] = {0};

    bool rendered = ui_overview_renderer_draw_mini(pixels, 4, 4, 45, &source, 8000);

    assert(rendered);
    assert(pixels[(45 - 1) * 4 + 0] != 0);
    assert(pixels[3 * 4 + 0] != 0);
    assert(pixels[12 * 4 + 0] != 0);
    assert(pixels[21 * 4 + 0] != 0);
}

static void test_mini_renderer_leaves_horizontal_gaps(void)
{
    uint8_t samples[8];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[8 * 45] = {0};

    bool rendered = ui_overview_renderer_draw_mini(pixels, 8, 8, 45, &source, 8000);

    assert(rendered);
    assert(pixels[(45 - 1) * 8 + 0] != 0);
    assert(pixels[(45 - 1) * 8 + 1] == 0);
    assert(pixels[(45 - 1) * 8 + 2] != 0);
    assert(pixels[(45 - 1) * 8 + 3] == 0);
}

int main(void)
{
    test_main_renderer_clears_and_draws_waveform_columns();
    test_main_renderer_keeps_downbeat_grid_on_top();
    test_main_renderer_draws_center_playhead();
    test_mini_renderer_clears_and_draws_full_track_waveform();
    test_mini_renderer_preserves_spikes_on_tall_canvas();
    test_mini_renderer_does_not_clip_hot_spikes();
    test_mini_renderer_anchors_body_to_bottom();
    test_mini_renderer_connects_peak_stem_to_body();
    test_mini_renderer_leaves_horizontal_gaps();

    puts("ui_overview_renderer tests passed");
    return 0;
}
