#pragma once
#include <pthread.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#define portMAX_DELAY UINT32_MAX
typedef pthread_mutex_t *SemaphoreHandle_t;
extern unsigned test_mutex_creations;
extern unsigned test_mutex_waits;
extern int test_fail_mutex_create;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    ++test_mutex_creations;
    if (test_fail_mutex_create) return NULL;
    SemaphoreHandle_t mutex = malloc(sizeof(*mutex));
    if (mutex && pthread_mutex_init(mutex, NULL) != 0) abort();
    return mutex;
}
static inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout)
{
    if (timeout != portMAX_DELAY) abort();
    __atomic_add_fetch(&test_mutex_waits, 1, __ATOMIC_RELAXED);
    return pthread_mutex_lock(mutex) == 0;
}
static inline int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    return pthread_mutex_unlock(mutex) == 0;
}
