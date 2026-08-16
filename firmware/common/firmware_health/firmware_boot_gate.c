#include "firmware_boot_gate.h"

void firmware_boot_gate_init(firmware_boot_gate_t *gate, uint16_t challenge)
{
    if (!gate) return;
    gate->challenge = challenge != 0u ? challenge : 1u;
    gate->critical_tasks_alive = false;
    gate->p4_acknowledged = false;
}

void firmware_boot_gate_set_critical_tasks_alive(firmware_boot_gate_t *gate,
                                                 bool alive)
{
    if (!gate) return;
    gate->critical_tasks_alive = alive;
    if (!alive) gate->p4_acknowledged = false;
}

bool firmware_boot_gate_observe_p4_ack(firmware_boot_gate_t *gate,
                                       uint16_t response)
{
    if (!gate || !gate->critical_tasks_alive || response != gate->challenge) {
        return false;
    }
    gate->p4_acknowledged = true;
    return true;
}

bool firmware_boot_gate_ready(const firmware_boot_gate_t *gate)
{
    return gate && gate->critical_tasks_alive && gate->p4_acknowledged;
}

