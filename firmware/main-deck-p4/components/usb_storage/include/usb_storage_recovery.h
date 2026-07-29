#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool armed;
    bool session_connected;
    uint32_t observed_epoch;
    uint32_t completed_cycles;
    uint32_t last_cycle_tick;
} usb_storage_recovery_t;

void usb_storage_recovery_init(usb_storage_recovery_t *recovery,
                               bool session_connected,
                               uint32_t session_epoch,
                               uint32_t now_tick,
                               uint32_t completed_cycles);

void usb_storage_recovery_observe(usb_storage_recovery_t *recovery,
                                  bool session_connected,
                                  uint32_t session_epoch,
                                  uint32_t now_tick);

bool usb_storage_recovery_cycle_due(
    const usb_storage_recovery_t *recovery,
    uint32_t now_tick,
    uint32_t fast_interval_ticks,
    uint32_t fast_cycle_limit,
    uint32_t slow_interval_ticks);

void usb_storage_recovery_mark_cycle(usb_storage_recovery_t *recovery,
                                     uint32_t now_tick);

bool usb_storage_recovery_uses_slow_cadence(
    const usb_storage_recovery_t *recovery,
    uint32_t fast_cycle_limit);
