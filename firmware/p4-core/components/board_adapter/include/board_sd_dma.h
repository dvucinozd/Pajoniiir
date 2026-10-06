#pragma once
#include <stdbool.h>
#include "sdmmc_cmd.h"
/* Configure before mount, once per host. No fallback to external DMA. */
esp_err_t board_sd_dma_configure(sdmmc_host_t *host, bool internal_bounce);
