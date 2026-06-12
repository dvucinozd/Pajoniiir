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

static void test_phase_delta_reports_deck2_late(void)
{
    ui_beat_indicator_state_t deck1 = {
        .valid = true,
        .phase = 1,
        .progress_permille = 500,
    };
    ui_beat_indicator_state_t deck2 = {
        .valid = true,
        .phase = 1,
        .progress_permille = 250,
    };

    ui_beat_phase_delta_t delta = ui_beat_phase_delta_calculate(deck1, deck2);

    assert(delta.valid);
    assert(delta.deck2_delta_permille == -250);
}

static void test_phase_delta_wraps_at_bar_boundary(void)
{
    ui_beat_indicator_state_t deck1 = {
        .valid = true,
        .phase = 3,
        .progress_permille = 900,
    };
    ui_beat_indicator_state_t deck2 = {
        .valid = true,
        .phase = 0,
        .progress_permille = 100,
    };

    ui_beat_phase_delta_t delta = ui_beat_phase_delta_calculate(deck1, deck2);

    assert(delta.valid);
    assert(delta.deck2_delta_permille == 200);
}

static void test_phase_delta_marks_close_offsets_as_locked(void)
{
    ui_beat_indicator_state_t deck1 = {
        .valid = true,
        .phase = 2,
        .progress_permille = 480,
    };
    ui_beat_indicator_state_t deck2 = {
        .valid = true,
        .phase = 2,
        .progress_permille = 512,
    };

    ui_beat_phase_delta_t delta = ui_beat_phase_delta_calculate(deck1, deck2);

    assert(delta.valid);
    assert(delta.deck2_delta_permille == 32);
    assert(delta.locked);
}

int main(void)
{
    test_grid_position_uses_current_beat_phase();
    test_grid_marks_downbeat_phase_zero();
    test_bpm_fallback_without_grid();
    test_invalid_without_grid_or_bpm();
    test_phase_delta_reports_deck2_late();
    test_phase_delta_wraps_at_bar_boundary();
    test_phase_delta_marks_close_offsets_as_locked();

    puts("ui_beat_indicator tests passed");
    return 0;
}
