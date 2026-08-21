#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "control_link.h"

#define CONTROLLER_LED_RECONCILER_DECKS 2u
#define CONTROLLER_LED_RECONCILER_IDS   LED_REMOTE_COUNT

typedef struct {
    uint8_t state[CONTROLLER_LED_RECONCILER_DECKS]
                 [CONTROLLER_LED_RECONCILER_IDS];
    bool known[CONTROLLER_LED_RECONCILER_DECKS]
              [CONTROLLER_LED_RECONCILER_IDS];
    bool dirty[CONTROLLER_LED_RECONCILER_DECKS]
              [CONTROLLER_LED_RECONCILER_IDS];
    uint16_t scan_cursor;
    uint32_t retry_count;
} controller_led_reconciler_t;

typedef struct {
    uint8_t id;
    uint8_t deck;
    uint8_t state;
} controller_led_desired_t;

void controller_led_reconciler_reset(controller_led_reconciler_t *r);
bool controller_led_reconciler_observe(controller_led_reconciler_t *r,
                                       uint8_t id,
                                       uint8_t deck,
                                       uint8_t state);
bool controller_led_reconciler_next(controller_led_reconciler_t *r,
                                    controller_led_desired_t *out);
void controller_led_reconciler_complete(controller_led_reconciler_t *r,
                                        const controller_led_desired_t *item,
                                        bool sent);
void controller_led_reconciler_mark_all_dirty(controller_led_reconciler_t *r);
