#pragma once
#include <stddef.h>
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue);
int xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
