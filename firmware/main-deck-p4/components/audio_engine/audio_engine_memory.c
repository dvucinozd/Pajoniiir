#include "audio_engine_memory.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "service_log.h"

static uint32_t s_walk_max_us[3];
static uint32_t s_walk_records;

static uint32_t measured_largest(uint32_t caps, unsigned index, uint32_t *max_us)
{
    const int64_t start = esp_timer_get_time();
    uint32_t largest = heap_caps_get_largest_free_block(caps);
    const int64_t end = esp_timer_get_time();
    const uint32_t elapsed = end > start ? (uint32_t)(end - start) : 0;
    uint32_t previous = __atomic_load_n(&s_walk_max_us[index], __ATOMIC_RELAXED);
    while (elapsed > previous && !__atomic_compare_exchange_n(
            &s_walk_max_us[index], &previous, elapsed, false,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    *max_us = __atomic_load_n(&s_walk_max_us[index], __ATOMIC_RELAXED);
    /* This measures the whole query, including preemption. It does not claim
     * to measure interrupt-disabled time. Bound journal work to four records. */
    if (elapsed >= 500) {
        uint32_t records = __atomic_load_n(&s_walk_records, __ATOMIC_RELAXED);
        while (records < 4u) {
            if (__atomic_compare_exchange_n(&s_walk_records, &records, records + 1u,
                    false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                service_log_event(SERVICE_LOG_HEAP_WALK_SLOW, SERVICE_LOG_WARN, 4u,
                                  index, elapsed, (uint32_t)(start / 1000), largest, NULL);
                break;
            }
        }
    }
    return largest;
}

void audio_engine_snapshot_memory(audio_engine_diagnostics_snapshot_t *out)
{
    if (!out) return;
    out->heap_free = esp_get_free_heap_size();
    out->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->internal_largest_free = measured_largest(MALLOC_CAP_INTERNAL, 0, &out->heap_walk_max_us[0]);
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    out->dma_free = heap_caps_get_free_size(dma_caps);
    out->dma_min_free = heap_caps_get_minimum_free_size(dma_caps);
    out->dma_largest_free = measured_largest(dma_caps, 1, &out->heap_walk_max_us[1]);
    out->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest_free = measured_largest(MALLOC_CAP_SPIRAM, 2, &out->heap_walk_max_us[2]);
}
