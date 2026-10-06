/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2025-2026 p3a Contributors
 * Adapted from kayrozen/Pajoniiir 428b97dd4a175f03d3a172c8db9c4d5ed94195fb.
 * Compatibility shim for ESP-IDF 6.0.2 only; SPI retains the original path. */
#include "sd_idle_wait.h"
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "esp_private/sdmmc_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6, 0, 2)
#error "Re-evaluate SD idle workaround before upgrading ESP-IDF"
#endif
esp_err_t __real_sdmmc_wait_for_idle(sdmmc_card_t *card, uint32_t status);
#if CONFIG_PAJONIIIR_SD_IDLE_WAIT
static int64_t now_us(void *ctx) { (void)ctx; return esp_timer_get_time(); }
static esp_err_t status_cmd(void *ctx, uint32_t *status) {
    return sdmmc_send_cmd_send_status(ctx, status);
}
static void yield_tick(void *ctx) { (void)ctx; vTaskDelay(1); }
#endif
esp_err_t __wrap_sdmmc_wait_for_idle(sdmmc_card_t *card, uint32_t status)
{
    if (!card) return ESP_ERR_INVALID_ARG;
#if CONFIG_PAJONIIIR_SD_IDLE_WAIT
    if (!host_is_spi(card)) {
        const sd_idle_wait_ops_t ops = {now_us, sdmmc_ready_for_data, status_cmd, yield_tick};
        return sd_idle_wait_run(card, status, SDMMC_READY_FOR_DATA_TIMEOUT_US, &ops);
    }
#endif
    return __real_sdmmc_wait_for_idle(card, status);
}
