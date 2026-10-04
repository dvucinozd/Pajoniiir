/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2025-2026 p3a Contributors
 * Adapted from kayrozen/Pajoniiir 428b97dd (v321 component).
 * Callback boundary and saturating counters added for the P4 shared core. */
#include "sd_idle_wait.h"
#include <stddef.h>
#include <limits.h>
static sd_idle_wait_stats_t s_stats;
static void add(uint32_t *counter, uint32_t amount)
{
    uint32_t old = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (!__atomic_compare_exchange_n(counter, &old,
            amount > UINT32_MAX - old ? UINT32_MAX : old + amount, false,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
static void record(uint32_t polls, uint64_t duration, esp_err_t result)
{
    add(&s_stats.waits, 1u); add(&s_stats.polls, polls);
    if (result == ESP_ERR_TIMEOUT) add(&s_stats.timeouts, 1u);
    else if (result != ESP_OK) add(&s_stats.errors, 1u);
    uint32_t us = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
    uint32_t old = __atomic_load_n(&s_stats.max_wait_us, __ATOMIC_RELAXED);
    while (us > old && !__atomic_compare_exchange_n(&s_stats.max_wait_us,
        &old, us, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
void sd_idle_wait_get_stats(sd_idle_wait_stats_t *out)
{
    if (!out) return;
#define SNAP(field) out->field = __atomic_load_n(&s_stats.field, __ATOMIC_RELAXED)
    SNAP(waits); SNAP(polls); SNAP(timeouts); SNAP(errors); SNAP(max_wait_us);
#undef SNAP
}
esp_err_t sd_idle_wait_run(void *ctx, uint32_t status, uint32_t timeout_us,
                          const sd_idle_wait_ops_t *ops)
{
    if (!ops || !ops->now_us || !ops->ready || !ops->status || !ops->yield_tick)
        return ESP_ERR_INVALID_ARG;
    if (ops->ready(status)) return ESP_OK;
    const int64_t started = ops->now_us(ctx);
    uint32_t polls = 0;
    esp_err_t rc = ESP_OK;
    for (;;) {
        if (polls) ops->yield_tick(ctx);
        int64_t elapsed = ops->now_us(ctx) - started;
        if (elapsed < 0 || (uint64_t)elapsed >= timeout_us) {
            rc = ESP_ERR_TIMEOUT; break;
        }
        ++polls;
        rc = ops->status(ctx, &status);
        if (rc != ESP_OK || ops->ready(status)) break;
    }
    int64_t elapsed = ops->now_us(ctx) - started;
    record(polls, elapsed > 0 ? (uint64_t)elapsed : 0u, rc);
    return rc;
}
