#include "firmware_resources.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>

static firmware_resources_t s_resources = {.startup_phase="boot", .last_failed_phase="none"};

static void allocation_failed(size_t bytes, uint32_t caps, const char *function)
{
    (void)function;
    /* No allocation, filesystem, logging, or blocking in this heap callback. */
    __atomic_fetch_add(&s_resources.allocation_failures, 1u, __ATOMIC_RELAXED);
    if (!(caps & MALLOC_CAP_SPIRAM))
        __atomic_fetch_add(&s_resources.critical_allocation_failures, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_resources.last_failed_bytes, (uint32_t)bytes, __ATOMIC_RELAXED);
    __atomic_store_n(&s_resources.last_failed_caps, caps, __ATOMIC_RELAXED);
    __atomic_store_n(&s_resources.last_failed_phase,
        __atomic_load_n(&s_resources.startup_phase, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

esp_err_t firmware_resources_init(void)
{
    return heap_caps_register_failed_alloc_callback(allocation_failed);
}

void firmware_resources_phase(const char *static_phase)
{
    __atomic_store_n(&s_resources.startup_phase, static_phase, __ATOMIC_RELEASE);
}

void firmware_resources_sample_task(firmware_resource_task_t task)
{
    if ((unsigned)task >= FW_RESOURCE_TASK_COUNT) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t previous = __atomic_load_n(&s_resources.stack_sample_ms[task], __ATOMIC_RELAXED);
    if (previous && (uint32_t)(now - previous) < 1000u) return;
    uint32_t free_bytes = (uint32_t)uxTaskGetStackHighWaterMark(NULL); /* IDF uses bytes */
    uint32_t old = __atomic_load_n(&s_resources.stack_min_bytes[task], __ATOMIC_RELAXED);
    if (!previous || free_bytes < old)
        __atomic_store_n(&s_resources.stack_min_bytes[task], free_bytes, __ATOMIC_RELAXED);
    __atomic_store_n(&s_resources.stack_sample_ms[task], now ? now : 1u, __ATOMIC_RELEASE);
}

void firmware_resources_snapshot(firmware_resources_t *out)
{
    if (!out) return;
    out->allocation_failures = __atomic_load_n(&s_resources.allocation_failures, __ATOMIC_RELAXED);
    out->critical_allocation_failures = __atomic_load_n(&s_resources.critical_allocation_failures, __ATOMIC_RELAXED);
    out->last_failed_bytes = __atomic_load_n(&s_resources.last_failed_bytes, __ATOMIC_RELAXED);
    out->last_failed_caps = __atomic_load_n(&s_resources.last_failed_caps, __ATOMIC_RELAXED);
    out->startup_phase = __atomic_load_n(&s_resources.startup_phase, __ATOMIC_ACQUIRE);
    out->last_failed_phase = __atomic_load_n(&s_resources.last_failed_phase, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < FW_RESOURCE_TASK_COUNT; ++i) {
        out->stack_sample_ms[i] = __atomic_load_n(&s_resources.stack_sample_ms[i], __ATOMIC_ACQUIRE);
        out->stack_min_bytes[i] = __atomic_load_n(&s_resources.stack_min_bytes[i], __ATOMIC_RELAXED);
    }
}
