
#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
static inline void vTaskPrioritySet(TaskHandle_t task, UBaseType_t prio) {
    (void)task; (void)prio;
}
