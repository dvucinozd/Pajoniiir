#include "board_sd_dma.h"
#include "sd_transfer_stats.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static char internal[8192], external[1024];
static unsigned allocations, checks;
static bool allocation_fails = true;
static int64_t timer;
static esp_err_t transfer_result;
void *heap_caps_aligned_alloc(size_t align, size_t bytes, unsigned caps) {
    assert(align == 64 && bytes == 8192 && caps == 7u); ++allocations;
    return allocation_fails ? NULL : internal;
}
bool esp_ptr_external_ram(const void *buf) { return buf == external; }
bool sdmmc_host_check_buffer_alignment(int slot, const void *buf, size_t bytes) {
    assert(slot == 0 && buf == internal && bytes == 512); ++checks; return true;
}
int64_t esp_timer_get_time(void) { return timer; }
esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *card, void *buf, size_t start, size_t count) {
    assert(card && buf == external && start == 7 && count == 2); timer += 320; return transfer_result;
}
esp_err_t __real_sdmmc_write_sectors(sdmmc_card_t *card, const void *buf, size_t start, size_t count) {
    assert(card && buf == internal && start == 9 && count == 1); timer += 950; return transfer_result;
}
esp_err_t __wrap_sdmmc_read_sectors(sdmmc_card_t *, void *, size_t, size_t);
esp_err_t __wrap_sdmmc_write_sectors(sdmmc_card_t *, const void *, size_t, size_t);
int main(void) {
    sdmmc_host_t host = {0}, other = {0};
    assert(board_sd_dma_configure(NULL, true) == ESP_ERR_INVALID_ARG);
    assert(board_sd_dma_configure(&host, false) == ESP_OK && !allocations && !host.dma_aligned_buffer);
    assert(board_sd_dma_configure(&host, true) == ESP_ERR_NO_MEM && !host.dma_aligned_buffer);
    allocation_fails = false;
    assert(board_sd_dma_configure(&host, true) == ESP_OK);
    assert(host.dma_aligned_buffer == internal && host.unaligned_multi_block_rw_max_chunk_size == 16);
    assert(!host.check_buffer_alignment(0, external, 512) && !checks);
    assert(host.check_buffer_alignment(0, internal, 512) && checks == 1);
    assert(board_sd_dma_configure(&other, true) == ESP_OK && allocations == 2 && other.dma_aligned_buffer == internal);
    sdmmc_card_t card = {0};
    assert(__wrap_sdmmc_read_sectors(&card, external, 7, 2) == ESP_OK);
    transfer_result = ESP_FAIL;
    assert(__wrap_sdmmc_write_sectors(&card, internal, 9, 1) == ESP_FAIL);
    sd_transfer_stats_t stats; sd_transfer_get_stats(&stats);
    assert(stats.reads == 1 && stats.writes == 1 && stats.errors == 1);
    assert(stats.read_max_us == 320 && stats.write_max_us == 950);
    sd_transfer_get_stats(NULL);
    puts("PASS actual SD host bounce policy, mount allocation failure, reuse and read/write timing wrappers");
}
