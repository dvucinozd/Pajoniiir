#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
unsigned ulTaskNotifyTake(int clear_on_exit, TickType_t ticks);
int xTaskNotifyGive(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);
void vTaskDelete(TaskHandle_t task);
