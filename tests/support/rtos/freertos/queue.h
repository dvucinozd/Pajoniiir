#pragma once

#include "freertos/FreeRTOS.h"

/* Real FIFO storage with a real capacity, so a producer that overruns the queue
 * takes the same branch it would on hardware. Blocking sends/receives do not
 * suspend (there is no scheduler); a non-zero timeout simply fails once the
 * queue cannot satisfy the call, and the test advances the tick and retries.
 * portMAX_DELAY behaves the same way, which is what makes an unbounded wait
 * visible in a host test instead of hanging it. */

typedef struct fake_queue *QueueHandle_t;

QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
void          vQueueDelete(QueueHandle_t queue);

int xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
int xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t ticks);
int xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks);
int xQueueOverwrite(QueueHandle_t queue, const void *item);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
int xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks);

unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
unsigned uxQueueSpacesAvailable(QueueHandle_t queue);
void     xQueueReset(QueueHandle_t queue);

/* True when a send was rejected because the queue was full since the last
 * xQueueReset — lets a test assert the drop path was actually taken. */
int fake_queue_send_rejections(QueueHandle_t queue);
