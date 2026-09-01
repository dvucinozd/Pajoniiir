/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_usb_recovery_gate.h"

void controller_usb_recovery_gate_init(controller_usb_recovery_gate_t *gate)
{
    if (!gate) {
        return;
    }
    gate->pending = false;
    gate->fault_epochs = 0u;
}

bool controller_usb_recovery_gate_begin_fault(
    controller_usb_recovery_gate_t *gate)
{
    if (!gate || gate->pending) {
        return false;
    }
    gate->pending = true;
    gate->fault_epochs++;
    return true;
}

void controller_usb_recovery_gate_cancel(controller_usb_recovery_gate_t *gate)
{
    if (gate) {
        gate->pending = false;
    }
}

bool controller_usb_recovery_gate_pending(
    const controller_usb_recovery_gate_t *gate)
{
    return gate && gate->pending;
}

void controller_usb_recovery_gate_complete(
    controller_usb_recovery_gate_t *gate)
{
    if (gate) {
        gate->pending = false;
    }
}

uint32_t controller_usb_recovery_gate_fault_epochs(
    const controller_usb_recovery_gate_t *gate)
{
    return gate ? gate->fault_epochs : 0u;
}
