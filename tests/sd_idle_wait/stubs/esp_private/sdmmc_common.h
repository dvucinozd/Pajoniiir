#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef struct { bool spi; } sdmmc_card_t;
#define SDMMC_READY_FOR_DATA_TIMEOUT_US 5000000u
static inline bool host_is_spi(sdmmc_card_t *c) { return c->spi; }
static inline bool sdmmc_ready_for_data(uint32_t s) { return s == 1; }
esp_err_t sdmmc_send_cmd_send_status(sdmmc_card_t *, uint32_t *);
