#include "controller_led_reconciler.h"

#include <assert.h>
#include <stdio.h>

static void test_failed_send_stays_dirty_and_latest_state_wins(void)
{
    controller_led_reconciler_t r;
    controller_led_reconciler_reset(&r);
    assert(controller_led_reconciler_observe(&r, 1u, 0u, 1u));

    controller_led_desired_t item;
    assert(controller_led_reconciler_next(&r, &item));
    assert(item.id == 1u && item.deck == 0u && item.state == 1u);
    controller_led_reconciler_complete(&r, &item, false);
    assert(r.retry_count == 1u);

    assert(controller_led_reconciler_observe(&r, 1u, 0u, 0u));
    assert(controller_led_reconciler_next(&r, &item));
    assert(item.state == 0u);
    controller_led_reconciler_complete(&r, &item, true);
    assert(!controller_led_reconciler_next(&r, &item));
}

static void test_stale_completion_cannot_clear_newer_desired_state(void)
{
    controller_led_reconciler_t r;
    controller_led_reconciler_reset(&r);
    controller_led_desired_t old;
    assert(controller_led_reconciler_observe(&r, 14u, 1u, 1u));
    assert(controller_led_reconciler_next(&r, &old));
    assert(controller_led_reconciler_observe(&r, 14u, 1u, 2u));
    controller_led_reconciler_complete(&r, &old, true);

    controller_led_desired_t current;
    assert(controller_led_reconciler_next(&r, &current));
    assert(current.id == 14u && current.deck == 1u && current.state == 2u);
}

static void test_mark_all_dirty_replays_known_non_vu_state(void)
{
    controller_led_reconciler_t r;
    controller_led_reconciler_reset(&r);
    controller_led_desired_t item;
    assert(controller_led_reconciler_observe(&r, 0u, 0u, 1u));
    assert(controller_led_reconciler_next(&r, &item));
    controller_led_reconciler_complete(&r, &item, true);
    assert(!controller_led_reconciler_next(&r, &item));
    controller_led_reconciler_mark_all_dirty(&r);
    assert(controller_led_reconciler_next(&r, &item));
    assert(item.id == 0u && item.deck == 0u && item.state == 1u);
}

static void test_full_remote_led_range_is_reconciled(void)
{
    controller_led_reconciler_t r;
    controller_led_reconciler_reset(&r);
    controller_led_desired_t item;

    assert(CONTROLLER_LED_RECONCILER_IDS == LED_REMOTE_COUNT);
    assert(controller_led_reconciler_observe(&r, LED_BEAT_JUMP_PAD_1, 0u, 1u));
    assert(controller_led_reconciler_observe(&r, LED_BEAT_JUMP_SHIFT_HELPER_8,
                                             1u, 2u));
    assert(!controller_led_reconciler_observe(&r, LED_REMOTE_COUNT, 0u, 1u));

    assert(controller_led_reconciler_next(&r, &item));
    assert(item.id == LED_BEAT_JUMP_PAD_1 && item.deck == 0u && item.state == 1u);
    controller_led_reconciler_complete(&r, &item, true);
    assert(controller_led_reconciler_next(&r, &item));
    assert(item.id == LED_BEAT_JUMP_SHIFT_HELPER_8 &&
           item.deck == 1u && item.state == 2u);
    controller_led_reconciler_complete(&r, &item, true);
    assert(!controller_led_reconciler_next(&r, &item));

    controller_led_reconciler_mark_all_dirty(&r);
    assert(controller_led_reconciler_next(&r, &item));
    assert(item.id == LED_BEAT_JUMP_PAD_1);
    controller_led_reconciler_complete(&r, &item, true);
    assert(controller_led_reconciler_next(&r, &item));
    assert(item.id == LED_BEAT_JUMP_SHIFT_HELPER_8);
}

int main(void)
{
    test_failed_send_stays_dirty_and_latest_state_wins();
    test_stale_completion_cannot_clear_newer_desired_state();
    test_mark_all_dirty_replays_known_non_vu_state();
    test_full_remote_led_range_is_reconciled();
    puts("controller_led_reconciler tests passed");
    return 0;
}
