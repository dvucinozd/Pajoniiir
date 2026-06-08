#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_beat_indicator.h"

static void test_grid_position_uses_current_beat_phase(void)
{
    const anlz_beat_t beats[] = {
        {.beat_phase = 0, .bpm_x100 = 12000, .time_ms = 1000},
        {.beat_phase = 1, .bpm_x100 = 12000, .time_ms = 1500},
        {.beat_phase = 2, .bpm_x100 = 12000, .time_ms = 2000},
        {.beat_phase = 3, .bpm_x100 = 12000, .time_ms = 2500},
        {.beat_phase = 0, .bpm_x100 = 12000, .time_ms = 3000},
    };

    ui_beat_indicator_state_t state =
        ui_beat_indicator_calculate(1760, beats, 5, 0);

    assert(state.valid);
    assert(state.phase == 1);
    assert(!state.downbeat);
    assert(state.progress_permille == 520);
}

static void test_grid_marks_downbeat_phase_zero(void)
{
    const anlz_beat_t beats[] = {
        {.beat_phase = 3, .bpm_x100 = 12000, .time_ms = 2500},
        {.beat_phase = 0, .bpm_x100 = 12000, .time_ms = 3000},
        {.beat_phase = 1, .bpm_x100 = 12000, .time_ms = 3500},
    };

    ui_beat_indicator_state_t state =
        ui_beat_indicator_calculate(3025, beats, 3, 0);

    assert(state.valid);
    assert(state.phase == 0);
    assert(state.downbeat);
    assert(state.progress_permille == 50);
}

static void test_bpm_fallback_without_grid(void)
{
    ui_beat_indicator_state_t state =
        ui_beat_indicator_calculate(1250, NULL, 0, 120);

    assert(state.valid);
    assert(state.phase == 2);
    assert(!state.downbeat);
    assert(state.progress_permille == 500);
}

static void test_invalid_without_grid_or_bpm(void)
{
    ui_beat_indicator_state_t state =
        ui_beat_indicator_calculate(1250, NULL, 0, 0);

    assert(!state.valid);
}

int main(void)
{
    test_grid_position_uses_current_beat_phase();
    test_grid_marks_downbeat_phase_zero();
    test_bpm_fallback_without_grid();
    test_invalid_without_grid_or_bpm();

    puts("ui_beat_indicator tests passed");
    return 0;
}
