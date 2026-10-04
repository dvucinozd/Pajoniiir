#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "firmware_resources.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
static heap_caps_alloc_failed_hook_t hook;
static int64_t now_us;
static unsigned scans;
static uint32_t reserve;
esp_err_t heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook_t cb)
{ hook = cb; return ESP_OK; }
int64_t esp_timer_get_time(void) { return now_us; }
UBaseType_t uxTaskGetStackHighWaterMark(void *task)
{ assert(!task); ++scans; return reserve; }
int main(void)
{
    assert(firmware_resources_init() == ESP_OK && hook);
    hook(8192, MALLOC_CAP_SPIRAM, "optional-art");
    hook(4096, MALLOC_CAP_INTERNAL, "critical-task");
    firmware_resources_t r;
    firmware_resources_phase("ui");
    firmware_resources_snapshot(&r);
    assert(r.allocation_failures == 2 && r.critical_allocation_failures == 1);
    assert(r.last_failed_bytes == 4096 && r.last_failed_caps == MALLOC_CAP_INTERNAL);
    assert(!strcmp(r.startup_phase, "ui"));
    now_us = 1000000; reserve = 2048;
    firmware_resources_sample_task(FW_RESOURCE_LVGL);
    assert(scans == 1);
    now_us += 500000; reserve = 512;
    firmware_resources_sample_task(FW_RESOURCE_LVGL);
    assert(scans == 1);
    now_us += 500000;
    firmware_resources_sample_task(FW_RESOURCE_LVGL);
    firmware_resources_snapshot(&r);
    assert(scans == 2 && r.stack_min_bytes[FW_RESOURCE_LVGL] == 512);
    now_us += 1000000; reserve = 4096;
    firmware_resources_sample_task(FW_RESOURCE_LVGL);
    firmware_resources_sample_task(FW_RESOURCE_OUTPUT);
    firmware_resources_sample_task(FW_RESOURCE_TASK_COUNT);
    firmware_resources_snapshot(&r);
    assert(scans == 4 && r.stack_min_bytes[FW_RESOURCE_LVGL] == 512);
    assert(r.stack_min_bytes[FW_RESOURCE_OUTPUT] == 4096);
    assert(!r.stack_sample_ms[FW_RESOURCE_DECODE1]); /* unknown is not accepted */
    assert(r.stack_sample_ms[FW_RESOURCE_OUTPUT] == 3000);
    firmware_resources_snapshot(NULL);
    puts("firmware resource callback/throttle/lifecycle-safe samples passed");
    return 0;
}
