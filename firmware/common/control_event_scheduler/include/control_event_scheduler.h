#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONTROL_EVENT_FIFO_CAPACITY       32u
#define CONTROL_EVENT_CONTINUOUS_CAPACITY 32u

typedef struct {
    uint8_t type;
    uint8_t id;
    int16_t value;
} control_scheduled_event_t;

typedef enum {
    CONTROL_EVENT_LATEST_VALUE = 0,
    CONTROL_EVENT_ACCUMULATE_DELTA,
} control_event_continuous_mode_t;

typedef struct {
    uint32_t fifo_full;
    uint32_t continuous_coalesced;
    uint32_t continuous_slot_full;
    uint32_t jog_saturated;
    uint32_t max_fifo_depth;
} control_event_scheduler_stats_t;

typedef struct {
    bool assigned;
    bool dirty;
    control_scheduled_event_t event;
} control_event_continuous_slot_t;

typedef struct {
    control_scheduled_event_t fifo[CONTROL_EVENT_FIFO_CAPACITY];
    size_t fifo_head;
    size_t fifo_tail;
    size_t fifo_count;
    control_event_continuous_slot_t continuous[CONTROL_EVENT_CONTINUOUS_CAPACITY];
    size_t continuous_cursor;
    control_event_scheduler_stats_t stats;
} control_event_scheduler_t;

/* Caller owns synchronization. No function drains or rewrites the FIFO from
 * the producer side; there is exactly one dequeue API for the consumer. */
void control_event_scheduler_reset(control_event_scheduler_t *scheduler);
bool control_event_scheduler_enqueue_discrete(control_event_scheduler_t *scheduler,
                                              const control_scheduled_event_t *event);
bool control_event_scheduler_dequeue_discrete(control_event_scheduler_t *scheduler,
                                              control_scheduled_event_t *event);
bool control_event_scheduler_publish_continuous(control_event_scheduler_t *scheduler,
                                                const control_scheduled_event_t *event,
                                                control_event_continuous_mode_t mode);
bool control_event_scheduler_take_continuous(control_event_scheduler_t *scheduler,
                                             control_scheduled_event_t *event);
control_event_scheduler_stats_t control_event_scheduler_get_stats(
    const control_event_scheduler_t *scheduler);
