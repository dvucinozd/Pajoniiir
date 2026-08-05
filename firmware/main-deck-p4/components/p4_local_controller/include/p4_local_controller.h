/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t local_semantic_events;
    uint32_t local_queue_failures;
    uint32_t legacy_forwarded_events;
    uint32_t legacy_suppressed_events;
    uint32_t profile_activations;
    uint32_t profile_fallbacks;
    bool local_connected;
    bool bootstrap_started;
} p4_local_controller_diagnostics_t;

bool p4_local_controller_enabled(void);
void p4_local_controller_get_diagnostics(
    p4_local_controller_diagnostics_t *diag_out);

#ifdef __cplusplus
}
#endif
