#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overview_motion.h"

static void test_first_render_redraws(void)
{
    assert(ui_overview_motion_should_redraw(UINT32_MAX,
                                            0,
                                            0,
                                            4000,
                                            UI_WAVEFORM_SOURCE_HIGH,
                                            true));
}

static void test_window_change_redraws_immediately(void)
{
    assert(ui_overview_motion_should_redraw(1000,
                                            4000,
                                            1008,
                                            8000,
                                            UI_WAVEFORM_SOURCE_HIGH,
                                            true));
}

static void test_playing_waveform_uses_thirty_fps_cadence(void)
{
    assert(ui_overview_motion_redraw_step_ms(UI_WAVEFORM_SOURCE_HIGH, true) == 33u);
    assert(!ui_overview_motion_should_redraw(1000,
                                             4000,
                                             1032,
                                             4000,
                                             UI_WAVEFORM_SOURCE_HIGH,
                                             true));
    assert(ui_overview_motion_should_redraw(1000,
                                            4000,
                                            1033,
                                            4000,
                                            UI_WAVEFORM_SOURCE_HIGH,
                                            true));
}

static void test_low_resolution_fallback_uses_same_playing_cadence(void)
{
    assert(ui_overview_motion_redraw_step_ms(UI_WAVEFORM_SOURCE_LOW, true) == 33u);
    assert(ui_overview_motion_should_redraw(2000,
                                            4000,
                                            2033,
                                            4000,
                                            UI_WAVEFORM_SOURCE_LOW,
                                            true));
}

static void test_paused_waveform_keeps_coarser_cadence(void)
{
    assert(ui_overview_motion_redraw_step_ms(UI_WAVEFORM_SOURCE_HIGH, false) == 80u);
    assert(!ui_overview_motion_should_redraw(1000,
                                             4000,
                                             1079,
                                             4000,
                                             UI_WAVEFORM_SOURCE_HIGH,
                                             false));
    assert(ui_overview_motion_should_redraw(1000,
                                            4000,
                                            1080,
                                            4000,
                                            UI_WAVEFORM_SOURCE_HIGH,
                                            false));
}

static void test_missing_waveform_source_does_not_redraw_after_initial_render(void)
{
    assert(!ui_overview_motion_should_redraw(1000,
                                             4000,
                                             1200,
                                             4000,
                                             UI_WAVEFORM_SOURCE_NONE,
                                             true));
}

static void test_center_snap_quantizes_to_waveform_pixel_grid(void)
{
    assert(ui_overview_motion_snap_center_ms(1000, 8000, 620) == 1006);
    assert(ui_overview_motion_snap_center_ms(1006, 8000, 620) == 1006);
    assert(ui_overview_motion_snap_center_ms(1019, 8000, 620) == 1019);
}

int main(void)
{
    test_first_render_redraws();
    test_window_change_redraws_immediately();
    test_playing_waveform_uses_thirty_fps_cadence();
    test_low_resolution_fallback_uses_same_playing_cadence();
    test_paused_waveform_keeps_coarser_cadence();
    test_missing_waveform_source_does_not_redraw_after_initial_render();
    test_center_snap_quantizes_to_waveform_pixel_grid();

    puts("ui_overview_motion tests passed");
    return 0;
}
