#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overview_grid.h"

static uint16_t build_dense_beat_grid(anlz_beat_t *beats,
                                      uint16_t capacity,
                                      uint32_t duration_ms,
                                      uint16_t bpm)
{
    uint16_t count = 0;
    uint32_t time_ms = 0;
    uint32_t beat_len_ms = 60000u / bpm;

    while (count < capacity && time_ms < duration_ms) {
        beats[count].beat_phase = count % 4u;
        beats[count].bpm_x100 = bpm * 100u;
        beats[count].time_ms = time_ms;
        count++;
        time_ms += beat_len_ms;
    }

    return count;
}

static void test_dense_beat_grid_becomes_sparse_overview_guides(void)
{
    enum { width_px = 550, min_spacing_px = 48 };
    const uint32_t duration_ms = 492920u;
    anlz_beat_t beats[1200];
    int columns[64];

    uint16_t beat_count = build_dense_beat_grid(beats, 1200, duration_ms, 138u);
    size_t column_count = ui_overview_grid_build_columns(
        beats, beat_count, duration_ms, width_px, min_spacing_px,
        columns, sizeof(columns) / sizeof(columns[0]));

    assert(beat_count > width_px);
    assert(column_count > 0);
    assert(column_count <= 16);

    for (size_t i = 1; i < column_count; i++) {
        assert(columns[i] - columns[i - 1] >= min_spacing_px);
    }
}

static void test_regular_beats_are_not_overview_grid_guides(void)
{
    const anlz_beat_t beats[] = {
        {.beat_phase = 1, .bpm_x100 = 12800, .time_ms = 1000},
        {.beat_phase = 2, .bpm_x100 = 12800, .time_ms = 1500},
        {.beat_phase = 3, .bpm_x100 = 12800, .time_ms = 2000},
    };
    int columns[8];

    size_t column_count = ui_overview_grid_build_columns(
        beats, 3, 30000u, 550, 48, columns, sizeof(columns) / sizeof(columns[0]));

    assert(column_count == 0);
}

static void test_zoom_grid_styles_regular_beats_as_subtle_guides(void)
{
    ui_overview_grid_style_t style = ui_overview_grid_style_for_phase(2);

    assert(style.palette_index == 8u);
    assert(style.line_width_px == 1);
    assert(style.y0_permille > 0);
    assert(style.y1_permille < 1000);
}

static void test_zoom_grid_styles_downbeats_as_full_height_markers(void)
{
    ui_overview_grid_style_t style = ui_overview_grid_style_for_phase(0);

    assert(style.palette_index == 4u);
    assert(style.line_width_px == 2);
    assert(style.y0_permille == 0);
    assert(style.y1_permille == 1000);
}

int main(void)
{
    test_dense_beat_grid_becomes_sparse_overview_guides();
    test_regular_beats_are_not_overview_grid_guides();
    test_zoom_grid_styles_regular_beats_as_subtle_guides();
    test_zoom_grid_styles_downbeats_as_full_height_markers();

    puts("ui_overview_grid tests passed");
    return 0;
}
