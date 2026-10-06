#include "usb_storage_recovery.h"

#include <limits.h>
#include <string.h>

void usb_storage_recovery_init(usb_storage_recovery_t *recovery,
                               bool session_connected,
                               uint32_t session_epoch,
                               uint32_t now_tick,
                               uint32_t completed_cycles)
{
    if (!recovery) {
        return;
    }

    memset(recovery, 0, sizeof(*recovery));
    recovery->session_connected = session_connected;
    recovery->observed_epoch = session_epoch;
    recovery->last_cycle_tick = now_tick;
    if (!session_connected) {
        recovery->armed = true;
        recovery->completed_cycles = completed_cycles;
    }
}

void usb_storage_recovery_observe(usb_storage_recovery_t *recovery,
                                  bool session_connected,
                                  uint32_t session_epoch,
                                  uint32_t now_tick)
{
    if (!recovery) {
        return;
    }

    if (session_connected) {
        recovery->armed = false;
        recovery->session_connected = true;
        recovery->observed_epoch = session_epoch;
        recovery->completed_cycles = 0u;
        return;
    }

    if (recovery->session_connected ||
        recovery->observed_epoch != session_epoch ||
        !recovery->armed) {
        /* A new disconnected epoch represents a new recovery opportunity.
         * Reset the fast-cycle budget instead of inheriting a lifetime latch
         * from a previous, successfully enumerated drive. */
        recovery->armed = true;
        recovery->session_connected = false;
        recovery->observed_epoch = session_epoch;
        recovery->completed_cycles = 0u;
        recovery->last_cycle_tick = now_tick;
    }
}

bool usb_storage_recovery_cycle_due(
    const usb_storage_recovery_t *recovery,
    uint32_t now_tick,
    uint32_t fast_interval_ticks,
    uint32_t fast_cycle_limit,
    uint32_t slow_interval_ticks)
{
    if (!recovery || !recovery->armed) {
        return false;
    }

    uint32_t interval = recovery->completed_cycles < fast_cycle_limit
                            ? fast_interval_ticks
                            : slow_interval_ticks;
    return (uint32_t)(now_tick - recovery->last_cycle_tick) >= interval;
}

void usb_storage_recovery_mark_cycle(usb_storage_recovery_t *recovery,
                                     uint32_t now_tick)
{
    if (!recovery || !recovery->armed) {
        return;
    }

    recovery->last_cycle_tick = now_tick;
    if (recovery->completed_cycles < UINT32_MAX) {
        recovery->completed_cycles++;
    }
}

bool usb_storage_recovery_uses_slow_cadence(
    const usb_storage_recovery_t *recovery,
    uint32_t fast_cycle_limit)
{
    return recovery &&
           recovery->armed &&
           recovery->completed_cycles >= fast_cycle_limit;
}
