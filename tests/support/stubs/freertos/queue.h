#pragma once

#include "freertos/FreeRTOS.h"

/* Level-1: a queue exists but never delivers. Sends succeed so a producer under
 * test is not forced down its drop path; receives fail so a consumer loop
 * terminates. Suites that care about queue *discipline* use tests/support/rtos.
 */

typedef void *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    (void)length;
    (void)item_size;
    return (QueueHandle_t)1;
}

static inline int xQueueSend(QueueHandle_t queue, const void *item, unsigned ticks)
{
    (void)queue;
    (void)item;
    (void)ticks;
    return pdTRUE;
}

static inline int xQueueSendToFront(QueueHandle_t queue, const void *item, unsigned ticks)
{
    (void)queue;
    (void)item;
    (void)ticks;
    return pdTRUE;
}

static inline int xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    (void)queue;
    (void)item;
    return pdTRUE;
}

static inline int xQueueReceive(QueueHandle_t queue, void *item, unsigned ticks)
{
    (void)queue;
    (void)item;
    (void)ticks;
    return pdFALSE;
}

static inline int xQueuePeek(QueueHandle_t queue, void *item, unsigned ticks)
{
    (void)queue;
    (void)item;
    (void)ticks;
    return pdFALSE;
}

static inline unsigned uxQueueMessagesWaiting(QueueHandle_t queue)
{
    (void)queue;
    return 0u;
}

static inline void xQueueReset(QueueHandle_t queue)
{
    (void)queue;
}

static inline void vQueueDelete(QueueHandle_t queue)
{
    (void)queue;
}
