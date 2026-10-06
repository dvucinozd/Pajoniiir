#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_PERFORMANCE_TARGET_DECK_COUNT 2u
#define UI_PERFORMANCE_TARGET_DEFAULT_DECK 0u

typedef struct {
    uint8_t active_deck;
} ui_performance_target_t;

void ui_performance_target_init(ui_performance_target_t *target);
void ui_performance_target_set(ui_performance_target_t *target, uint8_t deck);
uint8_t ui_performance_target_get(const ui_performance_target_t *target);
bool ui_performance_target_is_active(const ui_performance_target_t *target, uint8_t deck);

#ifdef __cplusplus
}
#endif
