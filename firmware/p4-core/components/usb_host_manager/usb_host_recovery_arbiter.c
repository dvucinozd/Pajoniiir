/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_host_recovery_arbiter.h"
#include <string.h>

static bool valid_port(uint8_t port)
{
    return port < USB_HOST_RECOVERY_PORT_COUNT;
}

static bool tick_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t bounded_delay(const usb_host_recovery_arbiter_t *arbiter,
                              uint32_t failures)
{
    uint32_t delay = arbiter->base_delay_ticks;
    if (delay == 0u) {
        return 0u;
    }
    for (uint32_t i = 1u; i < failures; ++i) {
        if (delay >= arbiter->max_delay_ticks || delay > UINT32_MAX / 2u) {
            return arbiter->max_delay_ticks;
        }
        delay *= 2u;
    }
    return delay > arbiter->max_delay_ticks ? arbiter->max_delay_ticks : delay;
}

void usb_host_recovery_arbiter_init(usb_host_recovery_arbiter_t *arbiter,
                                    uint32_t base_delay_ticks,
                                    uint32_t max_delay_ticks)
{
    if (!arbiter) {
        return;
    }
    memset(arbiter, 0, sizeof(*arbiter));
    arbiter->base_delay_ticks = base_delay_ticks;
    arbiter->max_delay_ticks =
        max_delay_ticks < base_delay_ticks ? base_delay_ticks : max_delay_ticks;
    arbiter->active_port = USB_HOST_RECOVERY_PORT_NONE;
}

bool usb_host_recovery_arbiter_request(usb_host_recovery_arbiter_t *arbiter,
                                       uint8_t port,
                                       usb_host_recovery_reason_t reason)
{
    if (!arbiter || !valid_port(port) || reason == USB_HOST_RECOVERY_REASON_NONE) {
        if (arbiter) {
            arbiter->invalid_requests++;
        }
        return false;
    }

    usb_host_recovery_port_state_t *state = &arbiter->ports[port];
    arbiter->requests++;
    if (state->pending || arbiter->active_port == port) {
        arbiter->coalesced_requests++;
        if (reason > state->reason) {
            state->reason = reason;
        }
        return true;
    }

    state->pending = true;
    state->retry_wait = false;
    state->reason = reason;
    state->next_eligible_tick = 0u;
    return true;
}

bool usb_host_recovery_arbiter_acquire(usb_host_recovery_arbiter_t *arbiter,
                                       uint32_t now_tick,
                                       uint8_t *port_out,
                                       usb_host_recovery_reason_t *reason_out)
{
    if (!arbiter || arbiter->active_port != USB_HOST_RECOVERY_PORT_NONE) {
        return false;
    }

    for (uint8_t offset = 0u; offset < USB_HOST_RECOVERY_PORT_COUNT; ++offset) {
        const uint8_t port =
            (uint8_t)((arbiter->next_preferred_port + offset) %
                      USB_HOST_RECOVERY_PORT_COUNT);
        usb_host_recovery_port_state_t *state = &arbiter->ports[port];
        if (!state->pending ||
            (state->retry_wait &&
             !tick_reached(now_tick, state->next_eligible_tick))) {
            continue;
        }

        state->pending = false;
        state->retry_wait = false;
        state->attempts++;
        arbiter->active_port = port;
        arbiter->next_preferred_port =
            (uint8_t)((port + 1u) % USB_HOST_RECOVERY_PORT_COUNT);
        if (port_out) {
            *port_out = port;
        }
        if (reason_out) {
            *reason_out = state->reason;
        }
        return true;
    }
    return false;
}

bool usb_host_recovery_arbiter_complete(usb_host_recovery_arbiter_t *arbiter,
                                        uint8_t port,
                                        bool success,
                                        uint32_t now_tick)
{
    if (!arbiter || !valid_port(port) || arbiter->active_port != port) {
        return false;
    }

    usb_host_recovery_port_state_t *state = &arbiter->ports[port];
    arbiter->active_port = USB_HOST_RECOVERY_PORT_NONE;
    if (success) {
        state->successes++;
        state->failures = 0u;
        state->pending = false;
        state->retry_wait = false;
        state->reason = USB_HOST_RECOVERY_REASON_NONE;
        state->next_eligible_tick = 0u;
        return true;
    }

    state->failures++;
    state->pending = true;
    state->retry_wait = true;
    const uint32_t delay = bounded_delay(arbiter, state->failures);
    state->next_eligible_tick = now_tick + delay;
    return true;
}

bool usb_host_recovery_arbiter_cancel(usb_host_recovery_arbiter_t *arbiter,
                                      uint8_t port)
{
    if (!arbiter || !valid_port(port) || arbiter->active_port == port) {
        return false;
    }
    memset(&arbiter->ports[port], 0, sizeof(arbiter->ports[port]));
    return true;
}

uint32_t usb_host_recovery_arbiter_retry_delay(
    const usb_host_recovery_arbiter_t *arbiter,
    uint8_t port)
{
    if (!arbiter || !valid_port(port) || !arbiter->ports[port].retry_wait) {
        return 0u;
    }
    return bounded_delay(arbiter, arbiter->ports[port].failures);
}
