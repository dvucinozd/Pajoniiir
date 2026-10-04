/* SPDX-License-Identifier: MIT */
/* LVGL 9.5 custom heap. Presentation allocations must not compete with
 * internal USB DMA descriptors and task stacks, including small labels/styles.
 * No internal fallback: allocation failure stays visible to LVGL's checks. */
#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if CONFIG_LV_USE_CUSTOM_MALLOC
#include "lvgl.h"
#include "esp_heap_caps.h"
#include <string.h>

#define UI_HEAP_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/* Pull this object from libui before LVGL's later archive is resolved. */
void ui_psram_allocator_keep(void) {}

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    (void)mem; (void)bytes;
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, UI_HEAP_CAPS);
}

void *lv_realloc_core(void *pointer, size_t size)
{
    return heap_caps_realloc(pointer, size, UI_HEAP_CAPS);
}

void lv_free_core(void *pointer) { heap_caps_free(pointer); }

void lv_mem_monitor_core(lv_mem_monitor_t *monitor)
{
    memset(monitor, 0, sizeof *monitor);
    monitor->total_size = heap_caps_get_total_size(UI_HEAP_CAPS);
    monitor->free_size = heap_caps_get_free_size(UI_HEAP_CAPS);
    monitor->free_biggest_size = heap_caps_get_largest_free_block(UI_HEAP_CAPS);
    if (monitor->total_size > 0u)
        monitor->used_pct = (uint8_t)(100u - monitor->free_size * 100u / monitor->total_size);
    if (monitor->free_size > 0u)
        monitor->frag_pct = (uint8_t)(100u - monitor->free_biggest_size * 100u / monitor->free_size);
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(UI_HEAP_CAPS, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}
#endif
#endif
