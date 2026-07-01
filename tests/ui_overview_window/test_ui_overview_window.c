#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overview_window.h"

static void test_fast_tracks_keep_minimum_smooth_window(void)
{
    assert(ui_overview_window_ms_from_bpm_x100(17400, 0) == 8000u);
    assert(ui_overview_window_ms_from_bpm_x100(24000, 0) == 8000u);
}

static void test_normal_tracks_keep_sixteen_beat_window(void)
{
    assert(ui_overview_window_ms_from_bpm_x100(12000, 0) == 8000u);
    assert(ui_overview_window_ms_from_bpm_x100(9000, 0) == 10656u);
}

static void test_missing_precise_bpm_uses_fallback_or_default(void)
{
    assert(ui_overview_window_ms_from_bpm_x100(0, 128) == 8000u);
    assert(ui_overview_window_ms_from_bpm_x100(0, 0) == 8000u);
}

static void test_extremely_slow_tracks_are_capped(void)
{
    assert(ui_overview_window_ms_from_bpm_x100(3000, 0) == 30000u);
}

static void test_zoom_delta_moves_in_coarse_steps_and_clamps(void)
{
    assert(ui_overview_zoom_step_default() == 2u);
    assert(ui_overview_zoom_visible_beats_for_step(0) == 8u);
    assert(ui_overview_zoom_visible_beats_for_step(1) == 12u);
    assert(ui_overview_zoom_visible_beats_for_step(2) == 16u);
    assert(ui_overview_zoom_visible_beats_for_step(3) == 24u);
    assert(ui_overview_zoom_visible_beats_for_step(4) == 32u);

    assert(ui_overview_zoom_apply_delta(2, 1) == 3u);
    assert(ui_overview_zoom_apply_delta(2, -1) == 1u);
    assert(ui_overview_zoom_apply_delta(2, 9) == 4u);
    assert(ui_overview_zoom_apply_delta(2, -9) == 0u);
}

static void test_zoom_step_scales_bpm_window_without_exceeding_bounds(void)
{
    assert(ui_overview_window_ms_from_bpm_x100_for_zoom(12000, 0, 0) == 4000u);
    assert(ui_overview_window_ms_from_bpm_x100_for_zoom(12000, 0, 1) == 6000u);
    assert(ui_overview_window_ms_from_bpm_x100_for_zoom(12000, 0, 2) == 8000u);
    assert(ui_overview_window_ms_from_bpm_x100_for_zoom(12000, 0, 3) == 12000u);
    assert(ui_overview_window_ms_from_bpm_x100_for_zoom(12000, 0, 4) == 16000u);
    assert(ui_overview_window_ms_from_bpm_x100_for_zoom(3000, 0, 4) == 30000u);
}

int main(void)
{
    test_fast_tracks_keep_minimum_smooth_window();
    test_normal_tracks_keep_sixteen_beat_window();
    test_missing_precise_bpm_uses_fallback_or_default();
    test_extremely_slow_tracks_are_capped();
    test_zoom_delta_moves_in_coarse_steps_and_clamps();
    test_zoom_step_scales_bpm_window_without_exceeding_bounds();

    puts("ui_overview_window tests passed");
    return 0;
}
