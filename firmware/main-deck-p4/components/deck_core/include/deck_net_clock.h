#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Monotonic millisecond stamps: comparisons are wrap safe within half-range. */
typedef struct {
    bool valid;
    uint32_t anchor_ms, period_us;
    uint8_t beat_in_bar, player;
    uint64_t source_epoch, master_epoch;
} deck_net_clock_t;
typedef enum {
    DECK_NET_SYNC_OFF=0, DECK_NET_SYNC_WAIT, DECK_NET_SYNC_ALIGNING, DECK_NET_SYNC_LOCKED
} deck_net_sync_status_t;
typedef struct {
    uint8_t sink;
    uint32_t sample_rate, latency_us;
    bool measured;
} deck_sink_latency_t;
typedef struct {
    bool loaded, playing, hold, sync, calibrated;
    uint32_t session, position_ms, captured_ms;
    float bar, bpm, pitch;
    bool grid;
} deck_net_local_snapshot_t;
