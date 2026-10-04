#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    FW_RESOURCE_OUTPUT, FW_RESOURCE_DECODE1, FW_RESOURCE_DECODE2,
    FW_RESOURCE_LOADER1, FW_RESOURCE_LOADER2, FW_RESOURCE_LVGL,
    FW_RESOURCE_CONTROLLER, FW_RESOURCE_STORAGE, FW_RESOURCE_HTTP,
    FW_RESOURCE_TASK_COUNT
} firmware_resource_task_t;

typedef struct {
    uint32_t allocation_failures, critical_allocation_failures;
    uint32_t last_failed_bytes, last_failed_caps;
    uint32_t stack_min_bytes[FW_RESOURCE_TASK_COUNT];
    uint32_t stack_sample_ms[FW_RESOURCE_TASK_COUNT];
    const char *startup_phase;
    const char *last_failed_phase;
} firmware_resources_t;

esp_err_t firmware_resources_init(void);
void firmware_resources_phase(const char *static_phase);
/* Task samples itself; no handle survives teardown. At most one stack scan
 * per second per role. Audio calls are throttled before entering this API. */
void firmware_resources_sample_task(firmware_resource_task_t task);
void firmware_resources_snapshot(firmware_resources_t *out);
