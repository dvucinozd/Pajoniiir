#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t main_redraw_budget;
    bool deck_order_flip;
} ui_overview_scheduler_t;

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
