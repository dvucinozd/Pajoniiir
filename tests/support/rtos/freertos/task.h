#pragma once

#include "freertos/FreeRTOS.h"

/* Tasks are registered, not scheduled. fake_rtos_pump() enters their bodies.
 * Notifications are per-task counting slots with the same take/give semantics
 * the firmware relies on, which is the part worth modelling: several components
 * use the default notification slot for their own control flow, and a fake that
 * always returned 0 would hide exactly the bug that costs. */

typedef struct fake_task *TaskHandle_t;

TickType_t xTaskGetTickCount(void);
void       taskYIELD(void);

/* Advances the tick by `ticks` and lets expired waits become satisfiable. */
void vTaskDelay(TickType_t ticks);

void         vTaskDelete(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
unsigned     uxTaskGetStackHighWaterMark(TaskHandle_t task);

int xTaskCreate(void (*body)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle);
int xTaskCreatePinnedToCore(void (*body)(void *), const char *name, unsigned stack,
                            void *arg, unsigned priority, TaskHandle_t *handle,
                            int core);

/* Take up to one notification. `clear_on_exit` zeroes the slot as FreeRTOS does.
 * Returns the count observed, 0 when none was pending. Does not block. */
unsigned ulTaskNotifyTake(int clear_on_exit, TickType_t ticks);
int      xTaskNotifyGive(TaskHandle_t task);

/* Pending notification count for a task, so a test can assert a late
 * acknowledgement did not leak into the next barrier. */
unsigned fake_task_notification_count(TaskHandle_t task);
