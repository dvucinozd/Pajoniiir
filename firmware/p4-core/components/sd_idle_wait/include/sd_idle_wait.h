/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    uint32_t waits, polls, timeouts, errors, max_wait_us;
} sd_idle_wait_stats_t;
typedef struct {
    int64_t (*now_us)(void *ctx);
    bool (*ready)(uint32_t status);
    esp_err_t (*status)(void *ctx, uint32_t *status);
    void (*yield_tick)(void *ctx);
} sd_idle_wait_ops_t;
/* Worker context only. No allocations; one status command per scheduler tick
 * after the initial probe. Timeout is checked again after yielding. */
esp_err_t sd_idle_wait_run(void *ctx, uint32_t status, uint32_t timeout_us,
                          const sd_idle_wait_ops_t *ops);
void sd_idle_wait_get_stats(sd_idle_wait_stats_t *out);
