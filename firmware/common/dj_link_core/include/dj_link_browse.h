#pragma once
#include "dj_link_db.h"
#include "dj_link_discovery.h"
#define DJ_LINK_BROWSE_PAGE_ROWS 8u
typedef enum { DJ_LINK_BROWSE_IDLE, DJ_LINK_BROWSE_LOADING,
    DJ_LINK_BROWSE_READY, DJ_LINK_BROWSE_FAILED, DJ_LINK_BROWSE_UNAVAILABLE } dj_link_browse_state_t;
typedef struct {
    uint64_t request_epoch, source_epoch, request_id;
    uint32_t revision, total, count, received, menu_id;
    uint8_t peer_number, slot, menu;
    dj_link_browse_state_t state;
    char error[32];
} dj_link_browse_status_t;
typedef struct {
    dj_link_db_t db;
    dj_link_db_io_t transport;
    dj_link_peer_track_t *rows; /* owner provides >= 2000 slots, never borrowed by UI */
    dj_link_browse_status_t status;
    uint32_t visible_first, visible_count;
} dj_link_browse_t;
void dj_link_browse_init(dj_link_browse_t *b, dj_link_peer_track_t *rows,
                         const dj_link_db_io_t *transport);
bool dj_link_browse_start(dj_link_browse_t *b, const dj_link_discovery_t *peers,
    uint8_t number, uint64_t source_epoch, uint8_t slot,
    dj_link_db_menu_t menu, uint32_t menu_id, uint32_t now);
void dj_link_browse_cancel(dj_link_browse_t *b);
/* Call before TCP delivery as well as publication; invalid sources are cleared. */
bool dj_link_browse_validate(dj_link_browse_t *b, const dj_link_discovery_t *peers);
void dj_link_browse_poll(dj_link_browse_t *b, const dj_link_discovery_t *peers, uint32_t now);
size_t dj_link_browse_page(dj_link_browse_t *b, uint32_t first,
    dj_link_browse_status_t *status, dj_link_peer_track_t out[DJ_LINK_BROWSE_PAGE_ROWS]);

typedef struct {
    uint8_t deck, sender, source, slot, destination;
    uint32_t received_ms;
    uint32_t track_id;
    uint64_t source_epoch;
} dj_link_incoming_load_t;
/* Route only known peers and a claimed destination; never ACK here.
 * Transport listens on a unicast-only status socket. UI performs admission. */
bool dj_link_browse_parse_load(const dj_link_discovery_t *peers, uint32_t sender_ip,
    const uint8_t *packet, size_t len, dj_link_incoming_load_t *out);
bool dj_link_browse_load_current(const dj_link_discovery_t *peers,
    const dj_link_incoming_load_t *request, uint32_t now_ms);
