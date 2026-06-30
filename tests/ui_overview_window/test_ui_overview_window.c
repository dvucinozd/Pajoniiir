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

int main(void)
{
    test_fast_tracks_keep_minimum_smooth_window();
    test_normal_tracks_keep_sixteen_beat_window();
    test_missing_precise_bpm_uses_fallback_or_default();
    test_extremely_slow_tracks_are_capped();

    puts("ui_overview_window tests passed");
    return 0;
}
