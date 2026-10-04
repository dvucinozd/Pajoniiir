#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct { int unused; } sdmmc_card_t;
typedef struct {
    void *dma_aligned_buffer;
    size_t unaligned_multi_block_rw_max_chunk_size;
    bool (*check_buffer_alignment)(int, const void *, size_t);
} sdmmc_host_t;
