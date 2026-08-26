#include "controller_led_reconciler.h"

#include <string.h>

void controller_led_reconciler_reset(controller_led_reconciler_t *r)
{
    if (r) memset(r, 0, sizeof(*r));
}

bool controller_led_reconciler_observe(controller_led_reconciler_t *r,
                                       uint8_t id,
                                       uint8_t deck,
                                       uint8_t state)
{
    if (!r || id >= CONTROLLER_LED_RECONCILER_IDS ||
        deck >= CONTROLLER_LED_RECONCILER_DECKS) return false;
    r->state[deck][id] = state;
    r->known[deck][id] = true;
    r->dirty[deck][id] = true;
    return true;
}

bool controller_led_reconciler_next(controller_led_reconciler_t *r,
                                    controller_led_desired_t *out)
{
    if (!r || !out) return false;
    const uint16_t count = CONTROLLER_LED_RECONCILER_DECKS *
                           CONTROLLER_LED_RECONCILER_IDS;
    for (uint16_t offset = 0u; offset < count; ++offset) {
        uint16_t flat = (uint16_t)((r->scan_cursor + offset) % count);
        uint8_t deck = (uint8_t)(flat / CONTROLLER_LED_RECONCILER_IDS);
        uint8_t id = (uint8_t)(flat % CONTROLLER_LED_RECONCILER_IDS);
        if (!r->known[deck][id] || !r->dirty[deck][id]) continue;
        out->id = id;
        out->deck = deck;
        out->state = r->state[deck][id];
        r->scan_cursor = (uint16_t)((flat + 1u) % count);
        return true;
    }
    return false;
}

void controller_led_reconciler_complete(controller_led_reconciler_t *r,
                                        const controller_led_desired_t *item,
                                        bool sent)
{
    if (!r || !item || item->id >= CONTROLLER_LED_RECONCILER_IDS ||
        item->deck >= CONTROLLER_LED_RECONCILER_DECKS) return;
    if (!sent) {
        r->retry_count++;
        return;
    }
    /* A newer desired state may have arrived while a producer was enqueueing
     * this item. Never clear that newer value's dirty bit. */
    if (r->known[item->deck][item->id] &&
        r->state[item->deck][item->id] == item->state) {
        r->dirty[item->deck][item->id] = false;
    }
}

void controller_led_reconciler_mark_all_dirty(controller_led_reconciler_t *r)
{
    if (!r) return;
    for (uint8_t deck = 0u; deck < CONTROLLER_LED_RECONCILER_DECKS; ++deck) {
        for (uint8_t id = 0u; id < CONTROLLER_LED_RECONCILER_IDS; ++id) {
            if (r->known[deck][id]) r->dirty[deck][id] = true;
        }
    }
}
