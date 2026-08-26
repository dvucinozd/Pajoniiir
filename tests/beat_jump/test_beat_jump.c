#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "beat_jump.h"
#include "rekordbox_anlz.h"

static void test_uses_nearest_beatgrid_entry(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 3000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 4000, .beat_phase = 3, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 4,
        .bpm = 120,
    };

    assert(beat_jump_calculate_target_ms(2200, 120, 1, &meta) == 3000);
    assert(beat_jump_calculate_target_ms(2800, 120, -1, &meta) == 2000);
}

static void test_clamps_beatgrid_edges(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 3000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 4000, .beat_phase = 3, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 4,
        .bpm = 120,
    };

    assert(beat_jump_calculate_target_ms(900, 120, -32, &meta) == 1000);
    assert(beat_jump_calculate_target_ms(3800, 120, 32, &meta) == 4000);
}

static void test_falls_back_to_bpm_and_clamps_zero(void)
{
    assert(beat_jump_calculate_target_ms(1000, 120, 4, NULL) == 3000);
    assert(beat_jump_calculate_target_ms(1000, 120, -8, NULL) == 0);
    assert(beat_jump_calculate_target_ms(1000, 0, 1, NULL) == 1500);
}

static void test_fractional_jump_uses_local_beatgrid_spacing(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 1501, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 2, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 3,
        .bpm = 120,
    };

    assert(beat_jump_calculate_fractional_target_ms(1010, 120, 1, 16, &meta) == 1032);
    assert(beat_jump_calculate_fractional_target_ms(1490, 120, -1, 16, &meta) == 1469);
    assert(beat_jump_calculate_fractional_target_ms(1490, 120, 1, 2, &meta) == 1751);
}

static void test_fractional_jump_falls_back_to_bpm_and_clamps(void)
{
    assert(beat_jump_calculate_fractional_target_ms(1000, 120, 1, 16, NULL) == 1032);
    assert(beat_jump_calculate_fractional_target_ms(1000, 120, -1, 16, NULL) == 968);
    assert(beat_jump_calculate_fractional_target_ms(10, 120, -1, 16, NULL) == 0);
    assert(beat_jump_calculate_fractional_target_ms(UINT32_MAX - 10u,
                                                    120,
                                                    1,
                                                    16,
                                                    NULL) == UINT32_MAX);
}

static void test_fractional_jump_clamps_beatgrid_edges(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 1500, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 2, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {.beats = beats, .beat_count = 3, .bpm = 120};

    assert(beat_jump_calculate_fractional_target_ms(900, 120, -1, 16, &meta) == 1000);
    assert(beat_jump_calculate_fractional_target_ms(2100, 120, 1, 16, &meta) == 2000);
    assert(beat_jump_calculate_fractional_target_ms(1234, 120, 0, 16, &meta) == 1234);
}

static void test_loop_duration_uses_local_beatgrid_spacing(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 1500, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 2500, .beat_phase = 3, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 4,
        .bpm = 120,
    };

    assert(beat_loop_calculate_duration_ms(1750, 120, 1, 1, &meta) == 500);
    assert(beat_loop_calculate_duration_ms(1750, 120, 4, 1, &meta) == 2000);
}

static void test_loop_duration_supports_fractional_lengths(void)
{
    assert(beat_loop_calculate_duration_ms(1000, 120, 1, 2, NULL) == 250);
    assert(beat_loop_calculate_duration_ms(1000, 120, 1, 4, NULL) == 125);
    assert(beat_loop_calculate_duration_ms(1000, 120, 1, 32, NULL) == 16);
}

static void test_loop_duration_falls_back_to_default_bpm(void)
{
    assert(beat_loop_calculate_duration_ms(1000, 0, 1, 1, NULL) == 500);
    assert(beat_loop_calculate_duration_ms(1000, 0, 2, 1, NULL) == 1000);
}

static void test_phase_align_uses_reference_beat_phase(void)
{
    anlz_beat_t target_beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 1500, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 2500, .beat_phase = 3, .bpm_x100 = 12000},
        {.time_ms = 3000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 3500, .beat_phase = 1, .bpm_x100 = 12000},
    };
    anlz_beat_t reference_beats[] = {
        {.time_ms = 8000, .beat_phase = 0, .bpm_x100 = 12800},
        {.time_ms = 8469, .beat_phase = 1, .bpm_x100 = 12800},
        {.time_ms = 8938, .beat_phase = 2, .bpm_x100 = 12800},
        {.time_ms = 9407, .beat_phase = 3, .bpm_x100 = 12800},
    };
    anlz_metadata_t target = {.beats = target_beats, .beat_count = 6, .bpm = 120};
    anlz_metadata_t reference = {.beats = reference_beats, .beat_count = 4, .bpm = 128};
    uint32_t out = 0;

    assert(beat_phase_align_target_ms(2600, &target, 8900, &reference, &out));
    assert(out == 1962);
}

static void test_phase_align_fails_without_both_beatgrids(void)
{
    anlz_beat_t target_beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
    };
    anlz_metadata_t target = {.beats = target_beats, .beat_count = 1, .bpm = 120};
    uint32_t out = 1234;

    assert(!beat_phase_align_target_ms(1000, &target, 1000, NULL, &out));
    assert(out == 1234);
}

int main(void)
{
    test_uses_nearest_beatgrid_entry();
    test_clamps_beatgrid_edges();
    test_falls_back_to_bpm_and_clamps_zero();
    test_fractional_jump_uses_local_beatgrid_spacing();
    test_fractional_jump_falls_back_to_bpm_and_clamps();
    test_fractional_jump_clamps_beatgrid_edges();
    test_loop_duration_uses_local_beatgrid_spacing();
    test_loop_duration_supports_fractional_lengths();
    test_loop_duration_falls_back_to_default_bpm();
    test_phase_align_uses_reference_beat_phase();
    test_phase_align_fails_without_both_beatgrids();
    puts("beat_jump tests passed");
    return 0;
}
