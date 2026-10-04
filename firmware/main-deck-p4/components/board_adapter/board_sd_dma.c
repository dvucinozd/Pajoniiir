#include "board_sd_dma.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
/* Adapted policy from kayrozen/Pajoniiir 428b97dd: all SD sector reads/writes
 * use internal bounce when USB DMA is in PSRAM. Retained across mount retries. */
static void *s_bounce;
static bool buffer_aligned(int slot, const void *buffer, size_t bytes)
{
    if (esp_ptr_external_ram(buffer)) return false;
    return sdmmc_host_check_buffer_alignment(slot, buffer, bytes);
}
esp_err_t board_sd_dma_configure(sdmmc_host_t *host, bool internal_bounce)
{
    if (!host) return ESP_ERR_INVALID_ARG;
    if (!internal_bounce) return ESP_OK;
    if (!s_bounce) s_bounce = heap_caps_aligned_alloc(64u, 8192u,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_bounce) return ESP_ERR_NO_MEM;
    host->dma_aligned_buffer = s_bounce;
    host->unaligned_multi_block_rw_max_chunk_size = 16u;
    host->check_buffer_alignment = buffer_aligned;
    return ESP_OK;
}
