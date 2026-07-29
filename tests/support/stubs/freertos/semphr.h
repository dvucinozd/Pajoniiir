#pragma once

#include "freertos/FreeRTOS.h"

/* Level-1: every take succeeds immediately, so a single-threaded suite walks
 * straight through firmware locking. Distinct non-NULL handles per constructor
 * keep "was this the mutex or the binary semaphore" debuggable. */

typedef void *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)1;
}

static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    return (SemaphoreHandle_t)2;
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return (SemaphoreHandle_t)3;
}

static inline SemaphoreHandle_t xSemaphoreCreateCounting(unsigned max, unsigned initial)
{
    (void)max;
    (void)initial;
    return (SemaphoreHandle_t)4;
}

static inline int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{
    (void)sem;
    (void)ticks;
    return pdTRUE;
}

static inline int xSemaphoreGive(SemaphoreHandle_t sem)
{
    (void)sem;
    return pdTRUE;
}

static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t sem, unsigned ticks)
{
    (void)sem;
    (void)ticks;
    return pdTRUE;
}

static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t sem)
{
    (void)sem;
    return pdTRUE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    (void)sem;
}
