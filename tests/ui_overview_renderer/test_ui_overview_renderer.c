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

    /* Beat sits at the centre, under the white centre playhead (cols 3-5), so
     * check the red triangle on its exposed sides and the white grid at the
     * bottom. */
    assert(pixels[0 * 8 + 1] == 9);   /* red downbeat triangle base (left side) */
    assert(pixels[0 * 8 + 7] == 9);   /* red downbeat triangle base (right side) */
    assert(pixels[9 * 8 + 4] == 4);   /* white downbeat grid stays on top at the bottom */
}

static void test_main_renderer_keeps_regular_beat_grid_behind_waveform(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    anlz_beat_t beats[] = {
        {.beat_phase = 2, .bpm_x100 = 12000, .time_ms = 2000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 1,
    };
    uint8_t pixels[8 * 12] = {0};

    ui_overview_renderer_draw_main(pixels, 8, 8, 12, &source, 8000, &meta, 4000, 8000);

    assert(pixels[0 * 8 + 2] == 8);   /* dim guide above the wave (regular beats have no cap) */
    assert(pixels[11 * 8 + 2] == 8);  /* dim guide below the wave */
    assert(pixels[6 * 8 + 2] != 8);   /* wave hides the guide in the middle */
}

static void test_main_renderer_draws_downbeat_triangle_at_bottom(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    anlz_beat_t beats[] = {
        {.beat_phase = 0, .bpm_x100 = 12000, .time_ms = 2000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 1,
    };
    uint8_t pixels[8 * 12] = {0};

    ui_overview_renderer_draw_main_with_options(pixels, 8, 8, 12, &source, 8000,
                                                &meta, 4000, 8000, true);

    assert(pixels[0 * 8 + 2] == 4);    /* white downbeat line at the top */
    assert(pixels[8 * 8 + 2] == 9);    /* red triangle apex points up into the wave */
    assert(pixels[11 * 8 + 2] == 9);   /* red triangle base at the bottom edge */
    assert(pixels[11 * 8 + 0] == 9);   /* base is wider than the 2px line */
    assert(pixels[8 * 8 + 0] != 9);    /* apex is 1px wide */
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

static void test_main_rgb565_renderer_maps_palette_directly(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989,
    };
    uint16_t pixels[8 * 10];
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++) {
        pixels[i] = 0xFFFF;
    }

    ui_overview_renderer_draw_main_rgb565(pixels, 8, 8, 10, &source, 8000,
                                          NULL, 4000, 8000, palette,
                                          sizeof(palette) / sizeof(palette[0]));

    for (int x = 0; x < 8; x++) {
        assert(pixels[(10 / 2) * 8 + x] == palette[3] ||
               pixels[(10 / 2) * 8 + x] == palette[4] ||
               pixels[(10 / 2) * 8 + x] == palette[8]);
    }
    assert(pixels[0 * 8 + 4] == palette[4]);
    assert(pixels[9 * 8 + 4] == palette[4]);
}

static void test_main_rgb565_renderer_keeps_regular_beat_grid_behind_waveform(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    anlz_beat_t beats[] = {
        {.beat_phase = 2, .bpm_x100 = 12000, .time_ms = 2000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 1,
    };
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989, 0xF800,
    };
    uint16_t pixels[8 * 12] = {0};

    ui_overview_renderer_draw_main_rgb565(pixels, 8, 8, 12, &source, 8000,
                                          &meta, 4000, 8000, palette,
                                          sizeof(palette) / sizeof(palette[0]));

    assert(pixels[0 * 8 + 2] == palette[8]);   /* dim guide above the wave (no cap now) */
    assert(pixels[11 * 8 + 2] == palette[8]);  /* dim guide below the wave */
    assert(pixels[6 * 8 + 2] != palette[8]);   /* wave hides the guide in the middle */
}

static void test_main_rgb565_renderer_draws_downbeat_triangle_at_bottom(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    anlz_beat_t beats[] = {
        {.beat_phase = 0, .bpm_x100 = 12000, .time_ms = 2000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 1,
    };
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989, 0xF800,
    };
    uint16_t pixels[8 * 12] = {0};

    ui_overview_renderer_draw_main_rgb565_with_options(pixels, 8, 8, 12,
                                                       &source, 8000, &meta,
                                                       4000, 8000, palette,
                                                       sizeof(palette) / sizeof(palette[0]),
                                                       true);

    assert(pixels[0 * 8 + 2] == palette[4]);   /* white downbeat line at the top */
    assert(pixels[8 * 8 + 2] == palette[9]);   /* red triangle apex points up */
    assert(pixels[11 * 8 + 2] == palette[9]);  /* red triangle base at the bottom edge */
    assert(pixels[11 * 8 + 0] == palette[9]);  /* base is wider than the line */
    assert(pixels[8 * 8 + 0] != palette[9]);   /* apex is 1px wide */
}

static void test_main_rgb565_renderer_can_draw_column_range_without_clearing_all(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989,
    };
    uint16_t pixels[8 * 10];
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++) {
        pixels[i] = 0xAAAA;
    }

    ui_overview_renderer_draw_main_rgb565_columns(pixels, 8, 8, 10,
                                                  6, 2,
                                                  &source, 8000, NULL,
                                                  4000, 8000, palette,
                                                  sizeof(palette) / sizeof(palette[0]));

    assert(pixels[0] == 0xAAAA);
    assert(pixels[(10 / 2) * 8 + 6] != 0xAAAA);
    assert(pixels[(10 / 2) * 8 + 7] != 0xAAAA);
}

static void test_main_rgb565_renderer_can_draw_logical_columns_to_destination_span(void)
{
    uint8_t samples[64];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989,
    };
    uint16_t pixels[64 * 10];
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++) {
        pixels[i] = 0xAAAA;
    }

    ui_overview_renderer_draw_main_rgb565_column_span(pixels, 64, 10,
                                                      40, 4, 8, 64,
                                                      &source, 8000, NULL,
                                                      4000, 8000, palette,
                                                      sizeof(palette) / sizeof(palette[0]),
                                                      false,
                                                      false, 0, 0);

    assert(pixels[(10 / 2) * 64 + 4] == 0xAAAA);
    assert(pixels[(10 / 2) * 64 + 40] != 0xAAAA);
}

static void test_main_rgb565_renderer_highlights_active_loop_region(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    /* Index 4 = white marker, index 10 = amber loop background. */
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989, 0xF8A8, 0x1234,
    };
    uint16_t pixels[64 * 10];
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++) {
        pixels[i] = 0xAAAA;
    }

    /* window [0,8000] over 64 px => 125 ms/px. Loop [2000,2500) => cols 16..19,
       start marker at col 16, end marker at col 20. Render logical 16..23. */
    ui_overview_renderer_draw_main_rgb565_column_span(pixels, 64, 10,
                                                      0, 16, 8, 64,
                                                      &source, 8000, NULL,
                                                      4000, 8000, palette,
                                                      sizeof(palette) / sizeof(palette[0]),
                                                      false,
                                                      true, 2000, 2500);

    /* Row 0 is above the bar (h=4, rows 3..7). Interior loop col 17 (dest 1) is
       amber; non-loop col 21 (dest 5) is background black. */
    assert(pixels[0 * 64 + 1] == 0x1234);
    assert(pixels[0 * 64 + 5] == 0x0000);
    /* Start/end edge markers are white across the full height. */
    assert(pixels[0 * 64 + 0] == 0xE71D);
    assert(pixels[9 * 64 + 0] == 0xE71D);
    assert(pixels[0 * 64 + 4] == 0xE71D);
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

static void test_main_renderer_spreads_isolated_transient_to_neighbor_columns(void)
{
    uint8_t samples[16] = {0};
    samples[6] = 0x1Fu;
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_HIGH,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint8_t pixels[16 * 24] = {0};

    ui_overview_renderer_draw_main(pixels, 16, 16, 24, &source, 16000, NULL, 8000, 16000);

    assert(column_height(pixels, 16, 24, 5) > 12);
    assert(column_height(pixels, 16, 24, 6) > 12);
    assert(column_height(pixels, 16, 24, 7) > 12);
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
    test_main_renderer_keeps_regular_beat_grid_behind_waveform();
    test_main_renderer_draws_downbeat_triangle_at_bottom();
    test_main_renderer_draws_center_playhead();
    test_main_renderer_spreads_isolated_transient_to_neighbor_columns();
    test_main_rgb565_renderer_maps_palette_directly();
    test_main_rgb565_renderer_keeps_regular_beat_grid_behind_waveform();
    test_main_rgb565_renderer_draws_downbeat_triangle_at_bottom();
    test_main_rgb565_renderer_can_draw_column_range_without_clearing_all();
    test_main_rgb565_renderer_can_draw_logical_columns_to_destination_span();
    test_main_rgb565_renderer_highlights_active_loop_region();
    test_mini_renderer_clears_and_draws_full_track_waveform();
    test_mini_renderer_preserves_spikes_on_tall_canvas();
    test_mini_renderer_does_not_clip_hot_spikes();
    test_mini_renderer_anchors_body_to_bottom();
    test_mini_renderer_connects_peak_stem_to_body();
    test_mini_renderer_leaves_horizontal_gaps();

    puts("ui_overview_renderer tests passed");
    return 0;
}
