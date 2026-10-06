/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pending;
    uint32_t fault_epochs;
} controller_usb_recovery_gate_t;

void controller_usb_recovery_gate_init(controller_usb_recovery_gate_t *gate);
bool controller_usb_recovery_gate_begin_fault(
    controller_usb_recovery_gate_t *gate);
void controller_usb_recovery_gate_cancel(controller_usb_recovery_gate_t *gate);
bool controller_usb_recovery_gate_pending(
    const controller_usb_recovery_gate_t *gate);
void controller_usb_recovery_gate_complete(
    controller_usb_recovery_gate_t *gate);
uint32_t controller_usb_recovery_gate_fault_epochs(
    const controller_usb_recovery_gate_t *gate);

#ifdef __cplusplus
}
#endif
