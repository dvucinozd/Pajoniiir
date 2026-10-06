#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Idle-screensaver timing, kept free of LVGL so it can be host-tested.
 *
 * The caller feeds it a monotonic millisecond clock plus the two inhibit
 * conditions, and gets back an edge: show, hide, or nothing. It owns no
 * rendering and no tab state.
 */

typedef enum {
    UI_IDLE_ACTION_NONE = 0,
    UI_IDLE_ACTION_SHOW,
    UI_IDLE_ACTION_HIDE,
} ui_idle_action_t;

typedef struct {
    /* 0 disables the screensaver; any pending one is hidden on the next tick. */
    uint32_t timeout_ms;
    uint32_t last_activity_ms;
    bool     shown;
} ui_idle_t;

void ui_idle_init(ui_idle_t *idle, uint32_t timeout_ms, uint32_t now_ms);

/* Changing the timeout restarts the countdown rather than back-dating it, so
 * raising it in Settings cannot blank the screen immediately. */
void ui_idle_set_timeout(ui_idle_t *idle, uint32_t timeout_ms, uint32_t now_ms);

/* Touch, controller event or web mutation. Safe to call at any rate. */
void ui_idle_notice_activity(ui_idle_t *idle, uint32_t now_ms);

/* Evaluate one tick. Playback and recording both inhibit and are treated as
 * activity, so the countdown starts fresh when they stop rather than firing
 * the moment a long track ends. */
ui_idle_action_t ui_idle_tick(ui_idle_t *idle,
                              uint32_t now_ms,
                              bool any_deck_playing,
                              bool recording_active);

bool ui_idle_is_shown(const ui_idle_t *idle);

/* Milliseconds until the screensaver would appear, for tests and diagnostics.
 * UINT32_MAX when it cannot appear from the current state. */
uint32_t ui_idle_remaining_ms(const ui_idle_t *idle,
                              uint32_t now_ms,
                              bool any_deck_playing,
                              bool recording_active);
