
#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
extern unsigned review_ticks;
void review_delay(void);
static inline TickType_t xTaskGetTickCount(void) { return review_ticks; }
static inline void vTaskDelay(TickType_t ticks) { review_ticks += ticks; review_delay(); }
static inline void vTaskPrioritySet(TaskHandle_t task, UBaseType_t prio) {
    (void)task; (void)prio;
}
