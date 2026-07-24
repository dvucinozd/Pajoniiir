#include "ui_idle.h"

#include <assert.h>
#include <stdio.h>

#define TIMEOUT_MS (2u * 60u * 1000u)

static void test_shows_only_after_the_full_timeout(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, TIMEOUT_MS, 1000u);

    assert(ui_idle_tick(&idle, 1000u + TIMEOUT_MS - 1u, false, false) ==
           UI_IDLE_ACTION_NONE);
    assert(!ui_idle_is_shown(&idle));

    assert(ui_idle_tick(&idle, 1000u + TIMEOUT_MS, false, false) ==
           UI_IDLE_ACTION_SHOW);
    assert(ui_idle_is_shown(&idle));

    /* The edge fires once; staying idle must not re-issue it. */
    assert(ui_idle_tick(&idle, 1000u + TIMEOUT_MS + 5000u, false, false) ==
           UI_IDLE_ACTION_NONE);
    assert(ui_idle_is_shown(&idle));
}

static void test_activity_dismisses_and_restarts_the_countdown(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, TIMEOUT_MS, 0u);
    assert(ui_idle_tick(&idle, TIMEOUT_MS, false, false) == UI_IDLE_ACTION_SHOW);

    ui_idle_notice_activity(&idle, TIMEOUT_MS + 10u);
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 10u, false, false) ==
           UI_IDLE_ACTION_HIDE);
    assert(!ui_idle_is_shown(&idle));

    /* Full timeout again from the activity, not from the original mark. */
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 10u + TIMEOUT_MS - 1u, false, false) ==
           UI_IDLE_ACTION_NONE);
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 10u + TIMEOUT_MS, false, false) ==
           UI_IDLE_ACTION_SHOW);
}

/* A deck playing for longer than the timeout must not blank the screen, and
 * must not leave a countdown that fires the moment it stops. */
static void test_playback_inhibits_and_rearms_the_countdown(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, TIMEOUT_MS, 0u);

    for (uint32_t t = 1000u; t <= 10u * TIMEOUT_MS; t += 1000u) {
        assert(ui_idle_tick(&idle, t, true, false) == UI_IDLE_ACTION_NONE);
        assert(!ui_idle_is_shown(&idle));
    }

    uint32_t stopped = 10u * TIMEOUT_MS;
    assert(ui_idle_tick(&idle, stopped + 1u, false, false) == UI_IDLE_ACTION_NONE);
    assert(ui_idle_tick(&idle, stopped + TIMEOUT_MS - 1u, false, false) ==
           UI_IDLE_ACTION_NONE);
    assert(ui_idle_tick(&idle, stopped + TIMEOUT_MS, false, false) ==
           UI_IDLE_ACTION_SHOW);
}

static void test_recording_inhibits_and_hides_an_active_screensaver(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, TIMEOUT_MS, 0u);
    assert(ui_idle_tick(&idle, TIMEOUT_MS, false, false) == UI_IDLE_ACTION_SHOW);

    /* Starting a recording while idle must bring the UI back rather than
     * leave the operator staring at a splash mid-capture. */
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 1u, false, true) ==
           UI_IDLE_ACTION_HIDE);
    assert(!ui_idle_is_shown(&idle));
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 2u, false, true) ==
           UI_IDLE_ACTION_NONE);
}

static void test_timeout_zero_disables_and_hides(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, TIMEOUT_MS, 0u);
    assert(ui_idle_tick(&idle, TIMEOUT_MS, false, false) == UI_IDLE_ACTION_SHOW);

    ui_idle_set_timeout(&idle, 0u, TIMEOUT_MS + 1u);
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 2u, false, false) ==
           UI_IDLE_ACTION_HIDE);
    /* Off stays off however long nothing happens. */
    assert(ui_idle_tick(&idle, TIMEOUT_MS + 100u * 60u * 1000u, false, false) ==
           UI_IDLE_ACTION_NONE);
    assert(!ui_idle_is_shown(&idle));
}

/* Raising the timeout in Settings must not blank the screen because the old,
 * shorter countdown had already elapsed. */
static void test_changing_the_timeout_restarts_rather_than_back_dates(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, 60u * 1000u, 0u);
    assert(ui_idle_tick(&idle, 59u * 1000u, false, false) == UI_IDLE_ACTION_NONE);

    ui_idle_set_timeout(&idle, 5u * 60u * 1000u, 59u * 1000u);
    assert(ui_idle_tick(&idle, 59u * 1000u + 5u * 60u * 1000u - 1u, false, false) ==
           UI_IDLE_ACTION_NONE);
    assert(ui_idle_tick(&idle, 59u * 1000u + 5u * 60u * 1000u, false, false) ==
           UI_IDLE_ACTION_SHOW);
}

/* esp_timer's millisecond counter wraps every 49.7 days; the deck is meant to
 * run longer than that, so the arithmetic has to survive the rollover. */
static void test_millisecond_clock_wrap_does_not_trigger_or_block(void)
{
    ui_idle_t idle;
    uint32_t before_wrap = 0xFFFFFFFFu - 1000u;
    ui_idle_init(&idle, TIMEOUT_MS, before_wrap);

    /* 1 s before the wrap and 999 ms after it: 2 s elapsed, far short. */
    assert(ui_idle_tick(&idle, 999u, false, false) == UI_IDLE_ACTION_NONE);
    assert(!ui_idle_is_shown(&idle));

    uint32_t after = before_wrap + TIMEOUT_MS;   /* wraps intentionally */
    assert(ui_idle_tick(&idle, after, false, false) == UI_IDLE_ACTION_SHOW);
}

static void test_remaining_reports_inhibits_and_counts_down(void)
{
    ui_idle_t idle;
    ui_idle_init(&idle, TIMEOUT_MS, 0u);
    assert(ui_idle_remaining_ms(&idle, 0u, false, false) == TIMEOUT_MS);
    assert(ui_idle_remaining_ms(&idle, 1000u, false, false) == TIMEOUT_MS - 1000u);
    assert(ui_idle_remaining_ms(&idle, 0u, true, false) == UINT32_MAX);
    assert(ui_idle_remaining_ms(&idle, 0u, false, true) == UINT32_MAX);

    ui_idle_set_timeout(&idle, 0u, 0u);
    assert(ui_idle_remaining_ms(&idle, 0u, false, false) == UINT32_MAX);
}

static void test_null_is_inert(void)
{
    ui_idle_init(NULL, TIMEOUT_MS, 0u);
    ui_idle_set_timeout(NULL, TIMEOUT_MS, 0u);
    ui_idle_notice_activity(NULL, 0u);
    assert(ui_idle_tick(NULL, 0u, false, false) == UI_IDLE_ACTION_NONE);
    assert(!ui_idle_is_shown(NULL));
    assert(ui_idle_remaining_ms(NULL, 0u, false, false) == UINT32_MAX);
}

int main(void)
{
    test_shows_only_after_the_full_timeout();
    test_activity_dismisses_and_restarts_the_countdown();
    test_playback_inhibits_and_rearms_the_countdown();
    test_recording_inhibits_and_hides_an_active_screensaver();
    test_timeout_zero_disables_and_hides();
    test_changing_the_timeout_restarts_rather_than_back_dates();
    test_millisecond_clock_wrap_does_not_trigger_or_block();
    test_remaining_reports_inhibits_and_counts_down();
    test_null_is_inert();
    puts("ui_idle tests passed");
    return 0;
}
