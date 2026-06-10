#include "ui_performance_target.h"

static bool deck_is_valid(uint8_t deck)
{
    return deck < UI_PERFORMANCE_TARGET_DECK_COUNT;
}

void ui_performance_target_init(ui_performance_target_t *target)
{
    if (!target) {
        return;
    }
    target->active_deck = UI_PERFORMANCE_TARGET_DEFAULT_DECK;
}

void ui_performance_target_set(ui_performance_target_t *target, uint8_t deck)
{
    if (!target || !deck_is_valid(deck)) {
        return;
    }
    target->active_deck = deck;
}

uint8_t ui_performance_target_get(const ui_performance_target_t *target)
{
    if (!target || !deck_is_valid(target->active_deck)) {
        return UI_PERFORMANCE_TARGET_DEFAULT_DECK;
    }
    return target->active_deck;
}

bool ui_performance_target_is_active(const ui_performance_target_t *target, uint8_t deck)
{
    return deck_is_valid(deck) && ui_performance_target_get(target) == deck;
}
