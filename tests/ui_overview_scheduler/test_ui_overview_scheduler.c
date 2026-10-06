#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overview_scheduler.h"

static void test_init_sets_empty_budget_and_default_order(void)
{
    ui_overview_scheduler_t scheduler = {
        .main_redraw_budget = 99,
        .deck_order_flip = true,
    };
    uint8_t first = 0xFF;
    uint8_t second = 0xFF;

    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);

    assert(scheduler.main_redraw_budget == 0);
    assert(first == 1);
    assert(second == 2);
}

static void test_single_redraw_budget_allows_exactly_one_consume(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_begin_tick(&scheduler, 1);

    assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(scheduler.main_redraw_budget == 0);
}

static void test_zero_redraw_budget_allows_no_consume(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_begin_tick(&scheduler, 0);

    assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(scheduler.main_redraw_budget == 0);
}

static void test_two_redraw_budget_allows_exactly_two_consumes(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_begin_tick(&scheduler, 2);

    assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(scheduler.main_redraw_budget == 0);
}

static void test_two_playing_decks_get_two_redraws_per_tick(void)
{
    assert(ui_overview_scheduler_budget_for_playing_decks(true, true) == 2);
}

static void test_single_or_no_playing_deck_keeps_one_redraw_budget(void)
{
    assert(ui_overview_scheduler_budget_for_playing_decks(true, false) == 1);
    assert(ui_overview_scheduler_budget_for_playing_decks(false, true) == 1);
    assert(ui_overview_scheduler_budget_for_playing_decks(false, false) == 1);
}

static void test_direct_overlay_is_allowed_for_both_decks(void)
{
    assert(ui_overview_scheduler_direct_overlay_allowed(0));
    assert(ui_overview_scheduler_direct_overlay_allowed(1));
    assert(!ui_overview_scheduler_direct_overlay_allowed(2));
}

static void test_deck_order_alternates_each_call(void)
{
    ui_overview_scheduler_t scheduler;
    uint8_t first = 0;
    uint8_t second = 0;
    ui_overview_scheduler_init(&scheduler);

    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);
    assert(first == 1);
    assert(second == 2);

    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);
    assert(first == 2);
    assert(second == 1);

    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);
    assert(first == 1);
    assert(second == 2);
}

static void test_null_arguments_are_safe(void)
{
    uint8_t first = 9;
    uint8_t second = 9;
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);

    ui_overview_scheduler_init(NULL);
    ui_overview_scheduler_begin_tick(NULL, 1);
    assert(!ui_overview_scheduler_try_consume_main_redraw(NULL));

    ui_overview_scheduler_next_deck_order(NULL, 1, 2, &first, &second);
    assert(first == 1);
    assert(second == 2);

    ui_overview_scheduler_next_deck_order(&scheduler, 3, 4, NULL, &second);
    assert(second == 4);

    ui_overview_scheduler_next_deck_order(&scheduler, 5, 6, &first, NULL);
    assert(first == 6);
}

static void test_waveform_phase_precedes_both_decks_chrome(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    scheduler.top_to_bottom = true;
    for (unsigned frame = 0; frame < 100; ++frame) {
        uint8_t first, second;
        ui_overview_update_step_t steps[4];
        ui_overview_scheduler_begin_tick(&scheduler, 2);
        ui_overview_scheduler_next_deck_order(&scheduler, 0, 1, &first, &second);
        assert(ui_overview_scheduler_plan_updates(first, second, true, steps) == 4);
        unsigned written = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (steps[i].phase == UI_OVERVIEW_PHASE_WAVEFORM) {
                assert(steps[i].deck == written);
                assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
                ++written;
            } else {
                assert(steps[i].phase == UI_OVERVIEW_PHASE_CHROME);
                assert(written == 2); /* No chrome can delay the other strip. */
                assert(steps[i].deck == i - 2);
            }
        }
        assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    }
}

static void test_combined_policy_and_paused_fairness_are_preserved(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    scheduler.top_to_bottom = true;
    for (unsigned frame = 0; frame < 4; ++frame) {
        uint8_t first, second;
        ui_overview_update_step_t steps[4];
        ui_overview_scheduler_begin_tick(&scheduler, 1);
        ui_overview_scheduler_next_deck_order(&scheduler, 0, 1, &first, &second);
        assert(first == (frame & 1) && second == 1 - first);
        assert(ui_overview_scheduler_plan_updates(first, second, false, steps) == 2);
        assert(steps[0].deck == first && steps[1].deck == second);
        assert(steps[0].phase == UI_OVERVIEW_PHASE_ALL);
        assert(steps[1].phase == UI_OVERVIEW_PHASE_ALL);
        assert(ui_overview_scheduler_plan_updates(first, second, true, steps) == 4);
        assert(steps[0].deck == first && steps[1].deck == second);
        assert(steps[2].deck == first && steps[3].deck == second);
    }
    assert(ui_overview_scheduler_plan_updates(0, 1, true, NULL) == 0);
}

int main(void)
{
    ui_overview_scheduler_t ordered;
    uint8_t first, second;
    ui_overview_scheduler_init(&ordered);
    ordered.top_to_bottom = true;
    for (unsigned i = 0; i < 4; ++i) {
        ui_overview_scheduler_begin_tick(&ordered, 2);
        ui_overview_scheduler_next_deck_order(&ordered, 0, 1, &first, &second);
        assert(first == 0 && second == 1);
    }
    ui_overview_scheduler_begin_tick(&ordered, 1);
    ui_overview_scheduler_next_deck_order(&ordered, 0, 1, &first, &second);
    assert(first == 0 && second == 1);
    ui_overview_scheduler_begin_tick(&ordered, 1);
    ui_overview_scheduler_next_deck_order(&ordered, 0, 1, &first, &second);
    assert(first == 1 && second == 0);
    test_init_sets_empty_budget_and_default_order();
    test_single_redraw_budget_allows_exactly_one_consume();
    test_zero_redraw_budget_allows_no_consume();
    test_two_redraw_budget_allows_exactly_two_consumes();
    test_two_playing_decks_get_two_redraws_per_tick();
    test_single_or_no_playing_deck_keeps_one_redraw_budget();
    test_direct_overlay_is_allowed_for_both_decks();
    test_deck_order_alternates_each_call();
    test_null_arguments_are_safe();
    test_waveform_phase_precedes_both_decks_chrome();
    test_combined_policy_and_paused_fairness_are_preserved();

    puts("ui_overview_scheduler tests passed");
    return 0;
}
