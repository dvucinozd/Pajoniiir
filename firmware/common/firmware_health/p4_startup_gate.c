#include "p4_startup_gate.h"

p4_startup_result_t p4_startup_gate_poll(const p4_startup_gate_t *gate,
                                        uint64_t now_us,
                                        bool core_ready,
                                        bool network_ready)
{
    if (!gate || now_us < gate->boot_started_us ||
        now_us - gate->boot_started_us >= P4_STARTUP_TIMEOUT_US) {
        return P4_STARTUP_TIMEOUT;
    }
    if (core_ready && (!gate->network_required || network_ready)) {
        return P4_STARTUP_READY;
    }
    return P4_STARTUP_WAIT;
}
