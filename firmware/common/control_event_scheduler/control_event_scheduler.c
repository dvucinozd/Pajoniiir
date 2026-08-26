#include "control_event_scheduler.h"

#include <limits.h>
#include <string.h>

void control_event_scheduler_reset(control_event_scheduler_t *scheduler)
{
    if (scheduler) {
        memset(scheduler, 0, sizeof(*scheduler));
    }
}

bool control_event_scheduler_enqueue_discrete(control_event_scheduler_t *scheduler,
                                              const control_scheduled_event_t *event)
{
    if (!scheduler || !event) {
        return false;
    }
    if (scheduler->fifo_count >= CONTROL_EVENT_FIFO_CAPACITY) {
        scheduler->stats.fifo_full++;
        return false;
    }
    scheduler->fifo[scheduler->fifo_tail] = *event;
    scheduler->fifo_tail = (scheduler->fifo_tail + 1u) % CONTROL_EVENT_FIFO_CAPACITY;
    scheduler->fifo_count++;
    if (scheduler->fifo_count > scheduler->stats.max_fifo_depth) {
        scheduler->stats.max_fifo_depth = (uint32_t)scheduler->fifo_count;
    }
    return true;
}

bool control_event_scheduler_dequeue_discrete(control_event_scheduler_t *scheduler,
                                              control_scheduled_event_t *event)
{
    if (!scheduler || !event || scheduler->fifo_count == 0u) {
        return false;
    }
    *event = scheduler->fifo[scheduler->fifo_head];
    scheduler->fifo_head = (scheduler->fifo_head + 1u) % CONTROL_EVENT_FIFO_CAPACITY;
    scheduler->fifo_count--;
    return true;
}

static control_event_continuous_slot_t *find_continuous_slot(
    control_event_scheduler_t *scheduler,
    const control_scheduled_event_t *event)
{
    control_event_continuous_slot_t *free_slot = NULL;
    for (size_t i = 0; i < CONTROL_EVENT_CONTINUOUS_CAPACITY; ++i) {
        control_event_continuous_slot_t *slot = &scheduler->continuous[i];
        if (slot->assigned && slot->event.type == event->type && slot->event.id == event->id) {
            return slot;
        }
        if (!slot->assigned && !free_slot) {
            free_slot = slot;
        }
    }
    return free_slot;
}

bool control_event_scheduler_publish_continuous(control_event_scheduler_t *scheduler,
                                                const control_scheduled_event_t *event,
                                                control_event_continuous_mode_t mode)
{
    if (!scheduler || !event) {
        return false;
    }
    control_event_continuous_slot_t *slot = find_continuous_slot(scheduler, event);
    if (!slot) {
        scheduler->stats.continuous_slot_full++;
        return false;
    }

    if (!slot->assigned) {
        slot->assigned = true;
        slot->event = *event;
        slot->dirty = true;
        return true;
    }

    /* A delivered relative delta has been consumed. The next producer value
     * starts a new accumulation window; it must not be added to history. */
    if (!slot->dirty) {
        slot->event = *event;
        slot->dirty = true;
        return true;
    }

    scheduler->stats.continuous_coalesced++;
    if (mode == CONTROL_EVENT_ACCUMULATE_DELTA) {
        const int32_t sum = (int32_t)slot->event.value + (int32_t)event->value;
        if (sum > INT16_MAX) {
            slot->event.value = INT16_MAX;
            scheduler->stats.jog_saturated++;
        } else if (sum < INT16_MIN) {
            slot->event.value = INT16_MIN;
            scheduler->stats.jog_saturated++;
        } else {
            slot->event.value = (int16_t)sum;
        }
    } else {
        slot->event = *event;
    }
    slot->dirty = true;
    return true;
}

bool control_event_scheduler_take_continuous(control_event_scheduler_t *scheduler,
                                             control_scheduled_event_t *event)
{
    if (!scheduler || !event) {
        return false;
    }
    for (size_t offset = 0; offset < CONTROL_EVENT_CONTINUOUS_CAPACITY; ++offset) {
        const size_t index = (scheduler->continuous_cursor + offset) %
                             CONTROL_EVENT_CONTINUOUS_CAPACITY;
        control_event_continuous_slot_t *slot = &scheduler->continuous[index];
        if (!slot->assigned || !slot->dirty) {
            continue;
        }
        *event = slot->event;
        slot->dirty = false;
        scheduler->continuous_cursor = (index + 1u) % CONTROL_EVENT_CONTINUOUS_CAPACITY;
        return true;
    }
    return false;
}

control_event_scheduler_stats_t control_event_scheduler_get_stats(
    const control_event_scheduler_t *scheduler)
{
    control_event_scheduler_stats_t stats = { 0 };
    if (scheduler) {
        stats = scheduler->stats;
    }
    return stats;
}
