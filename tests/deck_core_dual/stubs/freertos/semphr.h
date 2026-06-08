#pragma once

typedef void *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)1;
}

static inline int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{
    (void)sem;
    (void)ticks;
    return 1;
}

static inline int xSemaphoreGive(SemaphoreHandle_t sem)
{
    (void)sem;
    return 1;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    (void)sem;
}
