#include <assert.h>
#include <stdio.h>

#include "ui_controls.h"

static void test_defaults_to_deck_1_and_default_hot_cues(void)
{
    ui_controls_state_t state;
    ui_controls_state_init(&state);

    assert(ui_controls_active_deck(&state) == 0);
    assert(ui_controls_is_active_deck(&state, 0));
    assert(!ui_controls_is_active_deck(&state, 1));

    ui_controls_hot_cue_t cue_a = ui_controls_hot_cue(&state, 0);
    assert(!cue_a.empty);
    assert(cue_a.position_ms == 0);
    assert(cue_a.end_ms == 0);
    assert(cue_a.type == UI_CONTROLS_HOT_CUE_SINGLE);

    ui_controls_hot_cue_t cue_f = ui_controls_hot_cue(&state, 5);
    assert(cue_f.position_ms == 90000);
}

static void test_rejects_invalid_active_deck(void)
{
    ui_controls_state_t state;
    ui_controls_state_init(&state);

    assert(ui_controls_set_active_deck(&state, 1));
    assert(ui_controls_active_deck(&state) == 1);
    assert(!ui_controls_set_active_deck(&state, 42));
    assert(ui_controls_active_deck(&state) == 1);
}

static void test_tracks_loop_shadow_per_deck_and_active_deck(void)
{
    ui_controls_state_t state;
    ui_controls_state_init(&state);

    ui_controls_set_loop_shadow(&state, 1, true, 1000, 5000, 4);
    ui_controls_loop_state_t d1_loop = ui_controls_active_loop(&state);
    assert(!d1_loop.active);

    ui_controls_set_active_deck(&state, 1);
    ui_controls_loop_state_t d2_loop = ui_controls_active_loop(&state);
    assert(d2_loop.active);
    assert(d2_loop.start_ms == 1000);
    assert(d2_loop.end_ms == 5000);
    assert(d2_loop.beats == 4);
}

static void test_updates_and_clears_hot_cues(void)
{
    ui_controls_state_t state;
    ui_controls_state_init(&state);

    ui_controls_set_hot_cue(&state, 3, 12345, 23456, UI_CONTROLS_HOT_CUE_LOOP, false);
    ui_controls_hot_cue_t cue = ui_controls_hot_cue(&state, 3);
    assert(!cue.empty);
    assert(cue.position_ms == 12345);
    assert(cue.end_ms == 23456);
    assert(cue.type == UI_CONTROLS_HOT_CUE_LOOP);

    ui_controls_set_hot_cue(&state, 3, 0, 0, UI_CONTROLS_HOT_CUE_SINGLE, true);
    cue = ui_controls_hot_cue(&state, 3);
    assert(cue.empty);
    assert(cue.position_ms == UI_CONTROLS_EMPTY_HOT_CUE_MS);
}

int main(void)
{
    test_defaults_to_deck_1_and_default_hot_cues();
    test_rejects_invalid_active_deck();
    test_tracks_loop_shadow_per_deck_and_active_deck();
    test_updates_and_clears_hot_cues();

    puts("ui_controls tests passed");
    return 0;
}
