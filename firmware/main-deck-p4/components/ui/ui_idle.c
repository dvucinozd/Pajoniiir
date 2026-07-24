#include "ui_idle.h"

/* Unsigned subtraction, so a wrapping millisecond clock needs no special case:
 * esp_timer's ms counter wraps every 49.7 days and the deck is expected to run
 * far longer than that between reboots. */
static uint32_t elapsed_since(uint32_t now_ms, uint32_t mark_ms)
{
    return now_ms - mark_ms;
}

void ui_idle_init(ui_idle_t *idle, uint32_t timeout_ms, uint32_t now_ms)
{
    if (!idle) return;
    idle->timeout_ms = timeout_ms;
    idle->last_activity_ms = now_ms;
    idle->shown = false;
}

void ui_idle_set_timeout(ui_idle_t *idle, uint32_t timeout_ms, uint32_t now_ms)
{
    if (!idle) return;
    idle->timeout_ms = timeout_ms;
    idle->last_activity_ms = now_ms;
}

void ui_idle_notice_activity(ui_idle_t *idle, uint32_t now_ms)
{
    if (!idle) return;
    idle->last_activity_ms = now_ms;
}

ui_idle_action_t ui_idle_tick(ui_idle_t *idle,
                              uint32_t now_ms,
                              bool any_deck_playing,
                              bool recording_active)
{
    if (!idle) return UI_IDLE_ACTION_NONE;

    bool inhibited = any_deck_playing || recording_active;
    if (inhibited) {
        /* Treated as activity, not merely as a veto: otherwise a deck that
         * plays past the timeout would blank the screen the instant it stops. */
        idle->last_activity_ms = now_ms;
    }

    if (idle->timeout_ms == 0u || inhibited) {
        if (idle->shown) {
            idle->shown = false;
            return UI_IDLE_ACTION_HIDE;
        }
        return UI_IDLE_ACTION_NONE;
    }

    uint32_t elapsed = elapsed_since(now_ms, idle->last_activity_ms);
    if (!idle->shown && elapsed >= idle->timeout_ms) {
        idle->shown = true;
        return UI_IDLE_ACTION_SHOW;
    }
    if (idle->shown && elapsed < idle->timeout_ms) {
        idle->shown = false;
        return UI_IDLE_ACTION_HIDE;
    }
    return UI_IDLE_ACTION_NONE;
}

bool ui_idle_is_shown(const ui_idle_t *idle)
{
    return idle && idle->shown;
}

uint32_t ui_idle_remaining_ms(const ui_idle_t *idle,
                              uint32_t now_ms,
                              bool any_deck_playing,
                              bool recording_active)
{
    if (!idle || idle->timeout_ms == 0u || any_deck_playing || recording_active) {
        return UINT32_MAX;
    }
    if (idle->shown) return 0u;
    uint32_t elapsed = elapsed_since(now_ms, idle->last_activity_ms);
    return elapsed >= idle->timeout_ms ? 0u : idle->timeout_ms - elapsed;
}
