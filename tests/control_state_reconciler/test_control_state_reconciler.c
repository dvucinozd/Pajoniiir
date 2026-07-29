#include "control_link.h"
#include "control_state_reconciler.h"

#include <assert.h>
#include <stdio.h>

static unsigned s_checks;
#define CHECK(expr) do { s_checks++; assert(expr); } while (0)

static void test_held_semantic_keys_are_precise_and_independent(void)
{
    puts("== held semantic keys are precise and independent ==");
    const int touch = control_held_state_key(CTRL_ID_DECK1_JOG_TOUCH, 1);
    const int shift = control_held_state_key(CTRL_ID_DECK1_SHIFT, 1);
    const int censor = control_held_state_key(
        CTRL_ID_DECK1_EXT_ACTION,
        CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, true));
    const int pad_fx_1 = control_held_state_key(
        CTRL_ID_DECK1_PAD_ACTION,
        CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 3, false, true));
    const int pad_fx_2 = control_held_state_key(
        CTRL_ID_DECK1_PAD_ACTION,
        CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 3, false, true));
    const int roll = control_held_state_key(
        CTRL_ID_DECK2_PAD_ACTION,
        CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 3, true, true));

    CHECK(touch >= 0);
    CHECK(shift >= 0 && shift != touch);
    CHECK(censor >= 0 && censor != shift);
    CHECK(pad_fx_1 >= 0 && pad_fx_1 != censor);
    CHECK(pad_fx_2 >= 0 && pad_fx_2 != pad_fx_1);
    CHECK(roll >= 0 && roll != pad_fx_2);
    CHECK(control_held_state_key(CTRL_ID_DECK1_PLAY, 1) < 0);
    CHECK(control_held_state_key(
        CTRL_ID_DECK1_PAD_ACTION,
        CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 3, false, true)) < 0);
}

static void test_latest_physical_level_wins_before_delivery(void)
{
    puts("== press release press reconciles to the latest physical level ==");
    control_held_state_reconciler_t state;
    control_held_state_reset(&state);

    const int key =
        control_held_state_observe(&state, CTRL_ID_DECK1_JOG_TOUCH, 1, 10u);
    CHECK(key >= 0);
    control_held_state_mark_scheduled(&state, key, 1);
    CHECK(!state.slots[key].dirty);

    CHECK(control_held_state_observe(
              &state, CTRL_ID_DECK1_JOG_TOUCH, 0, 11u) == key);
    CHECK(state.slots[key].dirty);
    CHECK(control_held_state_observe(
              &state, CTRL_ID_DECK1_JOG_TOUCH, 1, 12u) == key);
    CHECK(!state.slots[key].dirty);

    CHECK(control_held_state_observe(
              &state, CTRL_ID_DECK1_JOG_TOUCH, 0, 13u) == key);
    size_t cursor = 0u;
    int dirty_key = -1;
    uint8_t id = 0u;
    uint8_t sequence = 0u;
    int16_t value = -1;
    CHECK(control_held_state_next_dirty(
        &state, &cursor, &dirty_key, &id, &value, &sequence));
    CHECK(dirty_key == key);
    CHECK(id == CTRL_ID_DECK1_JOG_TOUCH);
    CHECK(value == 0);
    CHECK(sequence == 13u);
}

static void test_scheduling_stale_snapshot_keeps_newer_state_dirty(void)
{
    puts("== scheduling an old snapshot cannot erase a newer desired level ==");
    control_held_state_reconciler_t state;
    control_held_state_reset(&state);

    const int key = control_held_state_observe(
        &state, CTRL_ID_DECK1_SHIFT, 1, 1u);
    CHECK(key >= 0);
    CHECK(control_held_state_observe(
              &state, CTRL_ID_DECK1_SHIFT, 0, 2u) == key);
    control_held_state_mark_scheduled(&state, key, 1);
    CHECK(state.slots[key].dirty);
    CHECK(state.slots[key].desired_value == 0);
}

static void test_disconnect_releases_every_observed_held_shape(void)
{
    puts("== disconnect converts simple ext and pad states to releases ==");
    control_held_state_reconciler_t state;
    control_held_state_reset(&state);

    const int simple = control_held_state_observe(
        &state, CTRL_ID_DECK2_SHIFT, 1, 1u);
    const int ext = control_held_state_observe(
        &state, CTRL_ID_DECK2_EXT_ACTION,
        CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, true), 2u);
    const int pad = control_held_state_observe(
        &state, CTRL_ID_DECK2_PAD_ACTION,
        CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 6, false, true), 3u);
    CHECK(simple >= 0 && ext >= 0 && pad >= 0);
    control_held_state_mark_scheduled(&state, simple, 1);
    control_held_state_mark_scheduled(
        &state, ext,
        CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, true));
    control_held_state_mark_scheduled(
        &state, pad,
        CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 6, false, true));

    control_held_state_release_all(&state, 44u);
    CHECK(state.slots[simple].desired_value == 0);
    CHECK(!CTRL_DECK_EXT_PRESSED(state.slots[ext].desired_value));
    CHECK(!CTRL_PAD_ACTION_PRESSED(state.slots[pad].desired_value));
    CHECK(state.slots[simple].dirty);
    CHECK(state.slots[ext].dirty);
    CHECK(state.slots[pad].dirty);
    CHECK(state.slots[pad].sequence == 44u);
}

int main(void)
{
    test_held_semantic_keys_are_precise_and_independent();
    test_latest_physical_level_wins_before_delivery();
    test_scheduling_stale_snapshot_keeps_newer_state_dirty();
    test_disconnect_releases_every_observed_held_shape();
    printf("TESTS_RUN=%u\n", s_checks);
    puts("control_state_reconciler tests passed");
    return 0;
}
