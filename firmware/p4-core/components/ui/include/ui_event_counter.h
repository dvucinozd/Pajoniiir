#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t requested;
} ui_event_counter_t;

void ui_event_counter_reset(ui_event_counter_t *counter);
uint32_t ui_event_counter_request(ui_event_counter_t *counter);
uint32_t ui_event_counter_sample(const ui_event_counter_t *counter);

static inline bool ui_event_counter_pending(uint32_t requested,
                                            uint32_t applied)
{
    return requested != applied;
}
