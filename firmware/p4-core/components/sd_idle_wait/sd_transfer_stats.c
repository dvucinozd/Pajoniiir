/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_transfer_stats.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"
#include <stdbool.h>
#include <stddef.h>
static sd_transfer_stats_t s_stats;
static void increment(uint32_t *p) {
    uint32_t old = __atomic_load_n(p, __ATOMIC_RELAXED);
    while (old != UINT32_MAX && !__atomic_compare_exchange_n(p, &old, old + 1u,
           false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
static void maximum(uint32_t *p, int64_t elapsed) {
    uint32_t us = elapsed <= 0 ? 0u : ((uint64_t)elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed);
    uint32_t old = __atomic_load_n(p, __ATOMIC_RELAXED);
    while (us > old && !__atomic_compare_exchange_n(p, &old, us, false,
           __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *, void *, size_t, size_t);
esp_err_t __real_sdmmc_write_sectors(sdmmc_card_t *, const void *, size_t, size_t);
esp_err_t __wrap_sdmmc_read_sectors(sdmmc_card_t *c, void *buf, size_t start, size_t count) {
    int64_t now = esp_timer_get_time();
    esp_err_t rc = __real_sdmmc_read_sectors(c, buf, start, count);
    increment(&s_stats.reads); maximum(&s_stats.read_max_us, esp_timer_get_time() - now);
    if (rc != ESP_OK) increment(&s_stats.errors);
    return rc;
}
esp_err_t __wrap_sdmmc_write_sectors(sdmmc_card_t *c, const void *buf, size_t start, size_t count) {
    int64_t now = esp_timer_get_time();
    esp_err_t rc = __real_sdmmc_write_sectors(c, buf, start, count);
    increment(&s_stats.writes); maximum(&s_stats.write_max_us, esp_timer_get_time() - now);
    if (rc != ESP_OK) increment(&s_stats.errors);
    return rc;
}
void sd_transfer_get_stats(sd_transfer_stats_t *out) {
    if (!out) return;
#define COPY(field) out->field = __atomic_load_n(&s_stats.field, __ATOMIC_RELAXED)
    COPY(reads); COPY(writes); COPY(errors); COPY(read_max_us); COPY(write_max_us);
#undef COPY
}
