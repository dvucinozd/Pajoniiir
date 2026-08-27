/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t local_semantic_events;
    uint32_t local_queue_failures;
    uint32_t profile_activations;
    uint32_t profile_fallbacks;
    bool local_connected;
    bool bootstrap_started;
} p4_local_controller_diagnostics_t;

esp_err_t p4_local_controller_start(void);
void p4_local_controller_get_diagnostics(
    p4_local_controller_diagnostics_t *diag_out);

#ifdef __cplusplus
}
#endif
