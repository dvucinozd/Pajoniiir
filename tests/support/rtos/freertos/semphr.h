#pragma once

#include "freertos/FreeRTOS.h"

/* Counting semantics are real: a binary semaphore starts empty, a mutex starts
 * available, and a recursive mutex tracks depth. A take that cannot be satisfied
 * fails rather than blocking, so a missing give shows up as a failed assertion
 * instead of a hung test. */

typedef struct fake_semaphore *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(unsigned max, unsigned initial);
void              vSemaphoreDelete(SemaphoreHandle_t sem);

int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
int xSemaphoreGive(SemaphoreHandle_t sem);
int xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks);
int xSemaphoreGiveRecursive(SemaphoreHandle_t sem);

unsigned uxSemaphoreGetCount(SemaphoreHandle_t sem);

/* Recursion depth currently held, for ownership assertions. */
unsigned fake_semaphore_recursion_depth(SemaphoreHandle_t sem);
