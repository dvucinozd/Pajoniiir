#pragma once

#include "freertos/FreeRTOS.h"

static inline TickType_t xTaskGetTickCount(void) { return 0; }

static inline int xTaskCreate(void (*task)(void *), const char *name,
                              unsigned stack, void *arg, unsigned priority,
                              void *handle)
{
    (void)task;
    (void)name;
    (void)stack;
    (void)arg;
    (void)priority;
    (void)handle;
    return pdPASS;
}
