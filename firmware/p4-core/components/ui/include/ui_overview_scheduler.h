#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t main_redraw_budget;
    bool deck_order_flip;
    bool top_to_bottom;
} ui_overview_scheduler_t;

typedef enum {
    UI_OVERVIEW_PHASE_ALL,
    UI_OVERVIEW_PHASE_WAVEFORM,
    UI_OVERVIEW_PHASE_CHROME,
} ui_overview_update_phase_t;

typedef struct {
    uint8_t deck;
    ui_overview_update_phase_t phase;
} ui_overview_update_step_t;

/* The scanout policy can reserve the early frame for both waveform writes.
 * Other boards retain their combined per-deck update. At most four steps. */
uint8_t ui_overview_scheduler_plan_updates(uint8_t first, uint8_t second,
                                           bool waveform_first,
                                           ui_overview_update_step_t steps[4]);

void ui_overview_scheduler_init(ui_overview_scheduler_t *scheduler);
void ui_overview_scheduler_begin_tick(ui_overview_scheduler_t *scheduler,
                                      uint8_t main_redraw_budget);
bool ui_overview_scheduler_try_consume_main_redraw(ui_overview_scheduler_t *scheduler);
uint8_t ui_overview_scheduler_budget_for_playing_decks(bool deck_a_playing,
                                                       bool deck_b_playing);
bool ui_overview_scheduler_direct_overlay_allowed(uint8_t deck);
void ui_overview_scheduler_next_deck_order(ui_overview_scheduler_t *scheduler,
                                           uint8_t deck_a,
                                           uint8_t deck_b,
                                           uint8_t *first,
                                           uint8_t *second);

#ifdef __cplusplus
}
#endif
