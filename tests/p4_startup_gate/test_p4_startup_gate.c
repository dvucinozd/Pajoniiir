#include <assert.h>
#include <stdio.h>
#include "p4_startup_gate.h"

int main(void)
{
    p4_startup_gate_t gate = {.boot_started_us = 1000, .network_required = true};
    assert(p4_startup_gate_poll(&gate, 1000, false, false) == P4_STARTUP_WAIT);
    assert(p4_startup_gate_poll(&gate, 2000, true, false) == P4_STARTUP_WAIT);
    assert(p4_startup_gate_poll(&gate, 2000, false, true) == P4_STARTUP_WAIT);
    assert(p4_startup_gate_poll(&gate, 2000, true, true) == P4_STARTUP_READY);
    assert(p4_startup_gate_poll(&gate, 1000 + P4_STARTUP_TIMEOUT_US - 1,
                              true, true) == P4_STARTUP_READY);
    assert(p4_startup_gate_poll(&gate, 1000 + P4_STARTUP_TIMEOUT_US,
                              true, true) == P4_STARTUP_TIMEOUT);
    assert(p4_startup_gate_poll(&gate, 1000 + P4_STARTUP_TIMEOUT_US,
                              true, false) == P4_STARTUP_TIMEOUT);
    gate.network_required = false;
    assert(p4_startup_gate_poll(&gate, 2000, true, false) == P4_STARTUP_READY);
    assert(p4_startup_gate_poll(&gate, 2000, false, false) == P4_STARTUP_WAIT);
    assert(p4_startup_gate_poll(&gate, 999, true, true) == P4_STARTUP_TIMEOUT);
    assert(p4_startup_gate_poll(NULL, 2000, true, true) == P4_STARTUP_TIMEOUT);
    /* Wide monotonic timestamps must not be truncated to a 32-bit clock. */
    gate.boot_started_us = UINT64_C(0x100000000);
    assert(p4_startup_gate_poll(&gate, gate.boot_started_us + 10,
                              true, false) == P4_STARTUP_READY);
    puts("p4_startup_gate: PASS");
    return 0;
}
