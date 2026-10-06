#include "ui_overview_scheduler.h"

uint8_t ui_overview_scheduler_plan_updates(uint8_t first, uint8_t second,
                                           bool waveform_first,
                                           ui_overview_update_step_t steps[4])
{
    if (!steps) return 0;
    if (waveform_first) {
        steps[0] = (ui_overview_update_step_t){first, UI_OVERVIEW_PHASE_WAVEFORM};
        steps[1] = (ui_overview_update_step_t){second, UI_OVERVIEW_PHASE_WAVEFORM};
        steps[2] = (ui_overview_update_step_t){first, UI_OVERVIEW_PHASE_CHROME};
        steps[3] = (ui_overview_update_step_t){second, UI_OVERVIEW_PHASE_CHROME};
        return 4;
    }
    steps[0] = (ui_overview_update_step_t){first, UI_OVERVIEW_PHASE_ALL};
    steps[1] = (ui_overview_update_step_t){second, UI_OVERVIEW_PHASE_ALL};
    return 2;
}

void ui_overview_scheduler_init(ui_overview_scheduler_t *scheduler)
{
    if (!scheduler) {
        return;
    }

    scheduler->main_redraw_budget = 0;
    scheduler->deck_order_flip = false;
    scheduler->top_to_bottom = false;
}

void ui_overview_scheduler_begin_tick(ui_overview_scheduler_t *scheduler,
                                      uint8_t main_redraw_budget)
{
    if (!scheduler) {
        return;
    }

    scheduler->main_redraw_budget = main_redraw_budget;
}

bool ui_overview_scheduler_try_consume_main_redraw(ui_overview_scheduler_t *scheduler)
{
    if (!scheduler || scheduler->main_redraw_budget == 0) {
        return false;
    }

    scheduler->main_redraw_budget--;
    return true;
}

uint8_t ui_overview_scheduler_budget_for_playing_decks(bool deck_a_playing,
                                                       bool deck_b_playing)
{
    return (deck_a_playing && deck_b_playing) ? 2u : 1u;
}

bool ui_overview_scheduler_direct_overlay_allowed(uint8_t deck)
{
    return deck < 2u;
}

void ui_overview_scheduler_next_deck_order(ui_overview_scheduler_t *scheduler,
                                           uint8_t deck_a,
                                           uint8_t deck_b,
                                           uint8_t *first,
                                           uint8_t *second)
{
    bool ordered = scheduler && scheduler->top_to_bottom && scheduler->main_redraw_budget >= 2u;
    bool flip = scheduler && !ordered && scheduler->deck_order_flip;
    if (first) {
        *first = flip ? deck_b : deck_a;
    }
    if (second) {
        *second = flip ? deck_a : deck_b;
    }
    if (scheduler && !ordered) {
        scheduler->deck_order_flip = !scheduler->deck_order_flip;
    }
}
