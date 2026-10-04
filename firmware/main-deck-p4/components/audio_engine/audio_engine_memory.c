#include "audio_engine_memory.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

void audio_engine_snapshot_memory(audio_engine_diagnostics_snapshot_t *out)
{
    if (!out) return;
    out->heap_free = esp_get_free_heap_size();
    out->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->internal_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    out->dma_free = heap_caps_get_free_size(dma_caps);
    out->dma_min_free = heap_caps_get_minimum_free_size(dma_caps);
    out->dma_largest_free = heap_caps_get_largest_free_block(dma_caps);
    out->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}
