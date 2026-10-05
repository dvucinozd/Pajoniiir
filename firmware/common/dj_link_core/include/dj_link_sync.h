#pragma once
#include "dj_link_discovery.h"
#include "deck_net_sync.h"

/* Heap-free, single network-worker owner. Deck task exchanges bounded copies.
 * Packet reception time is a monotonic uint32 millisecond clock (wrap safe).
 * No master is changed merely because a different peer starts advertising. */
typedef deck_net_local_snapshot_t dj_link_local_clock_t;
typedef bool (*dj_link_sync_send_t)(void *, uint16_t, uint32_t, const uint8_t *, size_t);
typedef struct {
    deck_net_clock_t clock;
    uint64_t network_epoch, master_epoch, peer_epoch;
    uint32_t own_ip;
    uint8_t own_numbers[2], own_mac[6];
    bool own_active;
    uint8_t peer, pending_number, yield_number;
    uint8_t remote_yield_number, remote_seen_number;
    uint64_t remote_yield_epoch;
    uint32_t remote_deadline_ms;
    int8_t local_master, pending_deck;
    uint32_t deadline_ms, status_ms, packet_counter[2], beat_rx_ms;
    uint32_t local_session[2];
    uint8_t last_beat[2];
    bool bound, have_status, have_beat, enabled;
    uint32_t rejected_beats, handoff_timeouts, rejected_controls;
} dj_link_sync_t;
void dj_link_sync_init(dj_link_sync_t *s);
/* Explicit SYNC engage allows a new reference; loss by itself never does. */
void dj_link_sync_follow(dj_link_sync_t *s);
bool dj_link_sync_master(dj_link_sync_t *s, const dj_link_discovery_t *d,
    unsigned deck, uint32_t now, dj_link_sync_send_t send, void *ctx);
bool dj_link_sync_ingest(dj_link_sync_t *s, const dj_link_discovery_t *d,
    uint32_t ip, const uint8_t *packet, size_t len, uint32_t now,
    dj_link_sync_send_t send, void *ctx);
void dj_link_sync_tick(dj_link_sync_t *s, const dj_link_discovery_t *d,
    const dj_link_local_clock_t local[2], uint32_t now,
    dj_link_sync_send_t send, void *ctx);
