/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HOST_RECOVERY_PORT_COUNT 2u
#define USB_HOST_RECOVERY_PORT_NONE  0xFFu

typedef enum {
    USB_HOST_RECOVERY_REASON_NONE = 0,
    USB_HOST_RECOVERY_REASON_ENUMERATION,
    USB_HOST_RECOVERY_REASON_TRANSFER,
    USB_HOST_RECOVERY_REASON_CLASS_TEARDOWN,
    USB_HOST_RECOVERY_REASON_MANUAL,
} usb_host_recovery_reason_t;

typedef struct {
    bool pending;
    bool retry_wait;
    usb_host_recovery_reason_t reason;
    uint32_t attempts;
    uint32_t successes;
    uint32_t failures;
    uint32_t next_eligible_tick;
} usb_host_recovery_port_state_t;

typedef struct {
    usb_host_recovery_port_state_t ports[USB_HOST_RECOVERY_PORT_COUNT];
    uint32_t base_delay_ticks;
    uint32_t max_delay_ticks;
    uint8_t active_port;
    uint8_t next_preferred_port;
    uint32_t requests;
    uint32_t coalesced_requests;
    uint32_t invalid_requests;
} usb_host_recovery_arbiter_t;

void usb_host_recovery_arbiter_init(usb_host_recovery_arbiter_t *arbiter,
                                    uint32_t base_delay_ticks,
                                    uint32_t max_delay_ticks);
bool usb_host_recovery_arbiter_request(usb_host_recovery_arbiter_t *arbiter,
                                       uint8_t port,
                                       usb_host_recovery_reason_t reason);
bool usb_host_recovery_arbiter_acquire(usb_host_recovery_arbiter_t *arbiter,
                                       uint32_t now_tick,
                                       uint8_t *port_out,
                                       usb_host_recovery_reason_t *reason_out);
bool usb_host_recovery_arbiter_complete(usb_host_recovery_arbiter_t *arbiter,
                                        uint8_t port,
                                        bool success,
                                        uint32_t now_tick);
bool usb_host_recovery_arbiter_cancel(usb_host_recovery_arbiter_t *arbiter,
                                      uint8_t port);
uint32_t usb_host_recovery_arbiter_retry_delay(
    const usb_host_recovery_arbiter_t *arbiter,
    uint8_t port);

#ifdef __cplusplus
}
#endif
