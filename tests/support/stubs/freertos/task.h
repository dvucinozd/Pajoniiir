#pragma once

#include "freertos/FreeRTOS.h"

/* Level-1: task creation reports success but nothing runs, and delays return
 * immediately. Suites that need a worker to actually execute use
 * tests/support/rtos. */

typedef void *TaskHandle_t;

static inline TickType_t xTaskGetTickCount(void) { return 0; }
static inline void taskYIELD(void) { }
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline void vTaskDelete(TaskHandle_t task) { (void)task; }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
static inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 1024u;
}

static inline int xTaskCreate(void (*task)(void *), const char *name,
                              unsigned stack, void *arg, unsigned priority,
                              TaskHandle_t *handle)
{
    (void)task;
    (void)name;
    (void)stack;
    (void)arg;
    (void)priority;
    if (handle) *handle = (TaskHandle_t)1;
    return pdPASS;
}

static inline int xTaskCreatePinnedToCore(void (*task)(void *), const char *name,
                                          unsigned stack, void *arg,
                                          unsigned priority, TaskHandle_t *handle,
                                          int core)
{
    (void)core;
    return xTaskCreate(task, name, stack, arg, priority, handle);
}

static inline unsigned ulTaskNotifyTake(int clear_on_exit, TickType_t ticks)
{
    (void)clear_on_exit;
    (void)ticks;
    return 0u;
}

static inline int xTaskNotifyGive(TaskHandle_t task)
{
    (void)task;
    return pdPASS;
}
