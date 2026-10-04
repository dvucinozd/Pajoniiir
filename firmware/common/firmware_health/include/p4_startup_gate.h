#pragma once

#include <stdbool.h>
#include <stdint.h>

#define P4_STARTUP_TIMEOUT_US UINT64_C(60000000)

typedef struct {
    uint64_t boot_started_us;
    bool network_required;
} p4_startup_gate_t;

typedef enum {
    P4_STARTUP_WAIT,
    P4_STARTUP_READY,
    P4_STARTUP_TIMEOUT,
} p4_startup_result_t;

/* The requirement is captured from saved settings, not a later UI toggle.
 * External USB devices and associated Wi-Fi clients are deliberately absent. */
p4_startup_result_t p4_startup_gate_poll(const p4_startup_gate_t *gate,
                                        uint64_t now_us,
                                        bool core_ready,
                                        bool network_ready);
