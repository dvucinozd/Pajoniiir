#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_position_interpolator.h"

static void test_first_sample_returns_snapshot(void)
{
    ui_position_interpolator_t interp;
    ui_position_interpolator_init(&interp);

    uint32_t pos = ui_position_interpolator_update(&interp,
                                                   1000,
                                                   10000,
                                                   true,
                                                   1000,
                                                   1000000);

    assert(pos == 1000);
}

static void test_playing_position_advances_between_identical_snapshots(void)
{
    ui_position_interpolator_t interp;
    ui_position_interpolator_init(&interp);

    assert(ui_position_interpolator_update(&interp, 1000, 10000, true, 1000, 1000000) == 1000);
    assert(ui_position_interpolator_update(&interp, 1000, 10000, true, 1000, 1033000) == 1033);
    assert(ui_position_interpolator_update(&interp, 1000, 10000, true, 1000, 1066000) == 1066);
}

static void test_pitch_speed_changes_interpolated_position(void)
{
    ui_position_interpolator_t interp;
    ui_position_interpolator_init(&interp);

    assert(ui_position_interpolator_update(&interp, 2000, 10000, true, 1050, 1000000) == 2000);
    assert(ui_position_interpolator_update(&interp, 2000, 10000, true, 1050, 1100000) == 2105);
}

static void test_paused_position_tracks_snapshot_without_advancing(void)
{
    ui_position_interpolator_t interp;
    ui_position_interpolator_init(&interp);

    assert(ui_position_interpolator_update(&interp, 3000, 10000, false, 1000, 1000000) == 3000);
    assert(ui_position_interpolator_update(&interp, 3000, 10000, false, 1000, 1500000) == 3000);
}

static void test_seek_or_cue_rebases_to_snapshot(void)
{
    ui_position_interpolator_t interp;
    ui_position_interpolator_init(&interp);

    assert(ui_position_interpolator_update(&interp, 5000, 10000, true, 1000, 1000000) == 5000);
    assert(ui_position_interpolator_update(&interp, 5000, 10000, true, 1000, 1100000) == 5100);
    assert(ui_position_interpolator_update(&interp, 0, 10000, true, 1000, 1110000) == 0);
    assert(ui_position_interpolator_update(&interp, 0, 10000, true, 1000, 1143000) == 33);
}

static void test_interpolated_position_clamps_to_duration(void)
{
    ui_position_interpolator_t interp;
    ui_position_interpolator_init(&interp);

    assert(ui_position_interpolator_update(&interp, 9900, 10000, true, 1000, 1000000) == 9900);
    assert(ui_position_interpolator_update(&interp, 9900, 10000, true, 1000, 1500000) == 10000);
}

int main(void)
{
    test_first_sample_returns_snapshot();
    test_playing_position_advances_between_identical_snapshots();
    test_pitch_speed_changes_interpolated_position();
    test_paused_position_tracks_snapshot_without_advancing();
    test_seek_or_cue_rebases_to_snapshot();
    test_interpolated_position_clamps_to_duration();

    puts("ui_position_interpolator tests passed");
    return 0;
}
