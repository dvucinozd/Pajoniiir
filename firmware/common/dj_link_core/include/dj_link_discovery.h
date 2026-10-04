#pragma once
#include "djlink/beat.h"
#include "djlink/status.h"

/* Single worker owns this heap-free model. Runtime copies snapshots;
 * callbacks never receive peer pointers. Epochs invalidate pending work. */
#define DJ_LINK_PEERS 8u
#define DJ_LINK_TIMEOUT_MS 5000u
#define DJ_LINK_CLAIM_MS 300u
#define DJ_LINK_KEEPALIVE_MS 2000u
typedef enum {
    DJ_LINK_OFF, DJ_LINK_WAIT_IP, DJ_LINK_ANNOUNCE, DJ_LINK_CLAIM_MAC,
    DJ_LINK_CLAIM_IP, DJ_LINK_CLAIM_FINAL, DJ_LINK_ACTIVE, DJ_LINK_OBSERVER
} dj_link_phase_t;
typedef enum { DJ_LINK_REASON_NONE, DJ_LINK_REASON_NO_PAIR } dj_link_reason_t;
typedef struct {
    bool present, has_mac, has_status, has_beat, has_position;
    uint8_t number, device_type, mac[6];
    char name[DJLINK_NAME_LEN + 1];
    uint32_t ip, last_ms, beat_rx_ms, position_rx_ms;
    uint64_t source_epoch;
    djlink_status_t status;
    djlink_beat_t beat;
    djlink_position_t position;
} dj_link_peer_t;
typedef struct {
    dj_link_phase_t phase;
    dj_link_reason_t reason;
    uint8_t numbers[2], iteration, sent_mask, mac[6];
    uint32_t ip, next_ms, busy_ms[6];
    bool busy[6];
    uint64_t epoch;
    uint32_t accepted, dropped, conflicts, peer_expirations;
    dj_link_peer_t peers[DJ_LINK_PEERS];
} dj_link_discovery_t;
void dj_link_discovery_init(dj_link_discovery_t *s);
/* Host-order IPv4. Changed enable/IP/MAC starts a new epoch and forgets peers. */
void dj_link_discovery_configure(dj_link_discovery_t *s, bool enabled,
                                uint32_t ip, const uint8_t mac[6], uint32_t now);
void dj_link_discovery_tick(dj_link_discovery_t *s, uint32_t now);
bool dj_link_discovery_ingest(dj_link_discovery_t *s, uint16_t port,
                              const uint8_t *buf, size_t len,
                              uint32_t source_ip, uint32_t now);
/* At most one port-50000 broadcast. Builder failure does not advance claims. */
int dj_link_discovery_poll(dj_link_discovery_t *s, uint32_t now,
                           uint8_t *out, size_t cap, unsigned *deck);
/* Valid only after both players completed their claims. */
uint8_t dj_link_discovery_number(const dj_link_discovery_t *s, unsigned deck);
bool dj_link_discovery_source_current(const dj_link_discovery_t *s,
                                      uint8_t number, uint64_t source_epoch);
