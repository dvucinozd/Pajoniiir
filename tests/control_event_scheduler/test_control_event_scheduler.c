#include "control_link.h"
#include "control_event_scheduler.h"
#include "control_state_reconciler.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static unsigned s_checks;
#define CHECK(expr) do { s_checks++; assert(expr); } while (0)

static control_scheduled_event_t event(uint8_t type, uint8_t id, int16_t value)
{
    const control_scheduled_event_t result = { type, id, value };
    return result;
}

static void test_discrete_fifo_survives_forced_producer_consumer_interleaving(void)
{
    puts("== discrete FIFO survives producer/consumer interleaving ==");
    control_event_scheduler_t scheduler;
    control_event_scheduler_reset(&scheduler);

    const control_scheduled_event_t expected[] = {
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, 1 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_CUE, 1 },
        { CTRL_TYPE_BUTTON, CTRL_ID_LOAD_DECK1, 1 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK2_CUE, 1 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_LOOP_IN, 1 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_LOOP_IN, 0 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_LOOP_OUT, 1 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_LOOP_OUT, 0 },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
          CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 2, false, true) },
        { CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
          CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 2, false, false) },
    };

    CHECK(control_event_scheduler_enqueue_discrete(&scheduler, &expected[0]));
    CHECK(control_event_scheduler_enqueue_discrete(&scheduler, &expected[1]));

    /* Forced interleaving seam: consumer runs after the producer's first
     * scheduling operation. Publishing a continuous event must not inspect,
     * drain, or rewrite the discrete FIFO. */
    control_scheduled_event_t actual;
    CHECK(control_event_scheduler_dequeue_discrete(&scheduler, &actual));
    CHECK(actual.id == expected[0].id);
    const control_scheduled_event_t fader =
        event(CTRL_TYPE_PITCH, CTRL_ID_CH1_VOLUME, 7000);
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &fader, CONTROL_EVENT_LATEST_VALUE));

    for (size_t i = 2; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        CHECK(control_event_scheduler_enqueue_discrete(&scheduler, &expected[i]));
    }
    for (size_t i = 1; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        CHECK(control_event_scheduler_dequeue_discrete(&scheduler, &actual));
        CHECK(actual.type == expected[i].type);
        CHECK(actual.id == expected[i].id);
        CHECK(actual.value == expected[i].value);
    }
    CHECK(!control_event_scheduler_dequeue_discrete(&scheduler, &actual));
    CHECK(control_event_scheduler_take_continuous(&scheduler, &actual));
    CHECK(actual.id == CTRL_ID_CH1_VOLUME && actual.value == 7000);
}

static void test_fifo_capacity_is_bounded_and_measured(void)
{
    puts("== FIFO capacity and high-water telemetry ==");
    control_event_scheduler_t scheduler;
    control_event_scheduler_reset(&scheduler);
    for (size_t i = 0; i < CONTROL_EVENT_FIFO_CAPACITY; ++i) {
        const control_scheduled_event_t item = event(CTRL_TYPE_BUTTON,
            (uint8_t)(CTRL_ID_DECK1_PLAY + (i & 1u)), (int16_t)i);
        CHECK(control_event_scheduler_enqueue_discrete(&scheduler, &item));
    }
    const control_scheduled_event_t overflow =
        event(CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PLAY, 1);
    CHECK(!control_event_scheduler_enqueue_discrete(&scheduler, &overflow));
    const control_event_scheduler_stats_t stats =
        control_event_scheduler_get_stats(&scheduler);
    CHECK(stats.fifo_full == 1u);
    CHECK(stats.max_fifo_depth == CONTROL_EVENT_FIFO_CAPACITY);
}

static void test_continuous_latest_and_jog_accumulator(void)
{
    puts("== continuous latest-value and saturating jog accumulation ==");
    control_event_scheduler_t scheduler;
    control_event_scheduler_reset(&scheduler);
    control_scheduled_event_t value = event(CTRL_TYPE_PITCH, CTRL_ID_CROSSFADER, 10);
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &value, CONTROL_EVENT_LATEST_VALUE));
    value.value = 20;
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &value, CONTROL_EVENT_LATEST_VALUE));
    value.value = 30;
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &value, CONTROL_EVENT_LATEST_VALUE));

    control_scheduled_event_t jog =
        event(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_BEND, 32760);
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &jog, CONTROL_EVENT_ACCUMULATE_DELTA));
    jog.value = 100;
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &jog, CONTROL_EVENT_ACCUMULATE_DELTA));

    control_scheduled_event_t actual;
    CHECK(control_event_scheduler_take_continuous(&scheduler, &actual));
    CHECK(actual.id == CTRL_ID_CROSSFADER && actual.value == 30);
    CHECK(control_event_scheduler_take_continuous(&scheduler, &actual));
    CHECK(actual.id == CTRL_ID_DECK1_JOG_BEND && actual.value == INT16_MAX);
    CHECK(!control_event_scheduler_take_continuous(&scheduler, &actual));

    jog.value = -7;
    CHECK(control_event_scheduler_publish_continuous(
        &scheduler, &jog, CONTROL_EVENT_ACCUMULATE_DELTA));
    CHECK(control_event_scheduler_take_continuous(&scheduler, &actual));
    CHECK(actual.id == CTRL_ID_DECK1_JOG_BEND && actual.value == -7);
    const control_event_scheduler_stats_t stats =
        control_event_scheduler_get_stats(&scheduler);
    CHECK(stats.continuous_coalesced == 3u);
    CHECK(stats.jog_saturated == 1u);
}

static void test_held_release_remains_durable_when_fifo_is_full(void)
{
    puts("== held release survives capacity+1 stalled FIFO ==");
    control_event_scheduler_t scheduler;
    control_event_scheduler_reset(&scheduler);
    for (size_t i = 0; i < CONTROL_EVENT_FIFO_CAPACITY; ++i) {
        const control_scheduled_event_t item =
            event(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_CUE, (int16_t)i);
        CHECK(control_event_scheduler_enqueue_discrete(&scheduler, &item));
    }

    control_held_state_reconciler_t held;
    control_held_state_reset(&held);
    const int key = control_held_state_observe(
        &held, CTRL_ID_DECK1_JOG_TOUCH, 1, 1u);
    CHECK(key >= 0);
    control_held_state_mark_scheduled(&held, key, 1);
    CHECK(control_held_state_observe(
        &held, CTRL_ID_DECK1_JOG_TOUCH, 0, 2u) == key);

    size_t cursor = 0u;
    int dirty_key = -1;
    uint8_t id = 0u;
    uint8_t sequence = 0u;
    int16_t release = 1;
    CHECK(control_held_state_next_dirty(
        &held, &cursor, &dirty_key, &id, &release, &sequence));
    CHECK(dirty_key == key);
    CHECK(id == CTRL_ID_DECK1_JOG_TOUCH);
    CHECK(release == 0);
    CHECK(sequence == 2u);
}

int main(void)
{
    test_discrete_fifo_survives_forced_producer_consumer_interleaving();
    test_fifo_capacity_is_bounded_and_measured();
    test_continuous_latest_and_jog_accumulator();
    test_held_release_remains_durable_when_fifo_is_full();
    printf("control_event_scheduler: %u checks passed\n", s_checks);
    return 0;
}
