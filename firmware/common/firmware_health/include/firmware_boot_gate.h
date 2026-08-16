#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t challenge;
    bool critical_tasks_alive;
    bool p4_acknowledged;
} firmware_boot_gate_t;

/* Pure state machine used by the S3 pending-image boot gate. A zero random
 * challenge is remapped so the wire value is never the default zero state. */
void firmware_boot_gate_init(firmware_boot_gate_t *gate, uint16_t challenge);
void firmware_boot_gate_set_critical_tasks_alive(firmware_boot_gate_t *gate,
                                                 bool alive);
bool firmware_boot_gate_observe_p4_ack(firmware_boot_gate_t *gate,
                                       uint16_t response);
bool firmware_boot_gate_ready(const firmware_boot_gate_t *gate);

