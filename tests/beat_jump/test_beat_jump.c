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

int main(void)
{
    test_uses_nearest_beatgrid_entry();
    test_clamps_beatgrid_edges();
    test_falls_back_to_bpm_and_clamps_zero();
    puts("beat_jump tests passed");
    return 0;
}
