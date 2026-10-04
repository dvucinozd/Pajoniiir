#include "dj_link_browse.h"
#include "djlink/mixer.h"
#include <string.h>
#include <stdio.h>
static int connect_peer(void *ctx, uint32_t ip, uint16_t port)
{
    dj_link_browse_t *b = ctx;
    return b->transport.connect(b->transport.ctx, ip, port);
}
static int send_peer(void *ctx, const uint8_t *bytes, size_t len)
{
    dj_link_browse_t *b = ctx;
    return b->transport.send(b->transport.ctx, bytes, len);
}
static void close_peer(void *ctx)
{
    dj_link_browse_t *b = ctx; b->transport.close(b->transport.ctx);
}
static void list_begin(void *ctx, uint32_t total)
{
    dj_link_browse_t *b = ctx;
    b->status.total = total; b->status.received = b->status.count = 0;
    ++b->status.revision;
}
static void track(void *ctx, uint32_t index, const dj_link_peer_track_t *row, bool detail)
{
    dj_link_browse_t *b = ctx;
    if (index >= DJ_LINK_DB_TRACK_LIMIT || (detail &&
        (index >= b->status.count || b->rows[index].rekordbox_id != row->rekordbox_id))) return;
    b->rows[index] = *row;
    if (!detail && index == b->status.received) ++b->status.received;
    ++b->status.revision;
}
static bool next_detail(void *ctx, uint32_t *index, uint32_t *id)
{
    dj_link_browse_t *b = ctx;
    if (b->status.state != DJ_LINK_BROWSE_READY ||
        !dj_link_db_pick_detail(b->rows, b->status.count, b->visible_first,
            b->visible_count, 0, 0, index)) return false;
    *id = b->rows[*index].rekordbox_id; return true;
}
void dj_link_browse_init(dj_link_browse_t *b, dj_link_peer_track_t *rows,
                         const dj_link_db_io_t *transport)
{
    memset(b, 0, sizeof(*b)); b->rows = rows; b->transport = *transport;
    dj_link_db_io_t io = {.connect=connect_peer, .send=send_peer, .close=close_peer,
        .list_begin=list_begin, .track=track, .next_detail=next_detail, .ctx=b};
    dj_link_db_init(&b->db, &io, DJ_LINK_DB_TRACK_LIMIT);
}
void dj_link_browse_cancel(dj_link_browse_t *b)
{
    dj_link_db_stop(&b->db);
    ++b->status.request_epoch; ++b->status.revision;
    b->status.state = DJ_LINK_BROWSE_IDLE;
    b->status.count = b->status.received = b->status.total = 0;
    b->visible_first = b->visible_count = 0; b->status.error[0] = 0;
}
bool dj_link_browse_start(dj_link_browse_t *b, const dj_link_discovery_t *peers,
    uint8_t number, uint64_t epoch, uint8_t slot, dj_link_db_menu_t menu, uint32_t id, uint32_t now)
{
    if (!b || !b->rows || !peers || !dj_link_discovery_number(peers, 0) ||
        !dj_link_discovery_source_current(peers, number, epoch) ||
        (slot != DJLINK_SLOT_USB && slot != DJLINK_SLOT_SD && slot != DJLINK_SLOT_LAPTOP) ||
        menu > DJ_LINK_DB_MENU_PLAYLIST) return false;
    const dj_link_peer_t *p = NULL;
    for (unsigned i=0; i<DJ_LINK_PEERS; ++i)
        if (peers->peers[i].present && peers->peers[i].number == number) p = &peers->peers[i];
    if (!p) return false;
    dj_link_browse_cancel(b);
    b->status.source_epoch=epoch; b->status.peer_number=number; b->status.slot=slot;
    b->status.menu=(uint8_t)menu; b->status.menu_id=id; b->status.state=DJ_LINK_BROWSE_LOADING;
    dj_link_db_set_menu(&b->db, menu, id);
    dj_link_db_start(&b->db, p->ip, number, slot, dj_link_discovery_number(peers,0), epoch, now);
    return true;
}
bool dj_link_browse_validate(dj_link_browse_t *b, const dj_link_discovery_t *peers)
{
    if (b->status.state == DJ_LINK_BROWSE_IDLE || b->status.state == DJ_LINK_BROWSE_UNAVAILABLE) return false;
    if (dj_link_discovery_number(peers,0) != b->db.our_number ||
        !dj_link_discovery_source_current(peers,b->status.peer_number,b->status.source_epoch)) {
        dj_link_browse_cancel(b); b->status.state=DJ_LINK_BROWSE_UNAVAILABLE;
        snprintf(b->status.error,sizeof(b->status.error),"SOURCE UNAVAILABLE"); return false;
    }
    return true;
}
void dj_link_browse_poll(dj_link_browse_t *b, const dj_link_discovery_t *peers, uint32_t now)
{
    if (!dj_link_browse_validate(b,peers)) return;
    dj_link_db_poll(&b->db,now);
    if (b->db.phase == DJ_LINK_DB_FAILED) {
        if (b->status.state != DJ_LINK_BROWSE_FAILED) ++b->status.revision;
        b->status.state=DJ_LINK_BROWSE_FAILED; b->status.count=0;
        snprintf(b->status.error,sizeof(b->status.error),"%s",dj_link_db_error(&b->db));
    } else if (b->db.list_done && b->status.state == DJ_LINK_BROWSE_LOADING) {
        b->status.count=b->status.received; b->status.state=DJ_LINK_BROWSE_READY;
        ++b->status.revision;
    }
}
size_t dj_link_browse_page(dj_link_browse_t *b, uint32_t first,
    dj_link_browse_status_t *status, dj_link_peer_track_t out[DJ_LINK_BROWSE_PAGE_ROWS])
{
    *status=b->status; memset(out,0,DJ_LINK_BROWSE_PAGE_ROWS*sizeof(*out));
    b->visible_first=first; b->visible_count=DJ_LINK_BROWSE_PAGE_ROWS;
    if (b->status.state != DJ_LINK_BROWSE_READY || first >= b->status.count) return 0;
    size_t n=b->status.count-first;
    if (n>DJ_LINK_BROWSE_PAGE_ROWS) n=DJ_LINK_BROWSE_PAGE_ROWS;
    memcpy(out,b->rows+first,n*sizeof(*out)); return n;
}
bool dj_link_browse_parse_load(const dj_link_discovery_t *peers, uint32_t ip,
    const uint8_t *packet, size_t len, dj_link_incoming_load_t *out)
{
    djlink_load_track_t load;
    if (!peers || !out || !dj_link_discovery_number(peers,0) ||
        djlink_load_track_parse(packet,len,&load)!=DJLINK_OK || !load.rekordbox_id ||
        packet[0x40]>=6 || (load.slot!=DJLINK_SLOT_USB && load.slot!=DJLINK_SLOT_SD &&
                            load.slot!=DJLINK_SLOT_LAPTOP)) return false;
    bool sender=false; const dj_link_peer_t *source=NULL; int deck=-1;
    for (unsigned d=0; d<2; ++d)
        if (packet[0x40]+1 == dj_link_discovery_number(peers,d)) deck=(int)d;
    for (unsigned i=0; i<DJ_LINK_PEERS; ++i) {
        const dj_link_peer_t *p=&peers->peers[i];
        if (!p->present) continue;
        if (p->number==load.sender_number && p->ip==ip) sender=true;
        if (p->number==load.target_player) source=p;
    }
    if (!sender || !source || deck<0) return false;
    *out=(dj_link_incoming_load_t){.deck=(uint8_t)deck,.sender=load.sender_number,
        .source=load.target_player,.slot=load.slot,.track_id=load.rekordbox_id,
        .destination=(uint8_t)(packet[0x40]+1),.source_epoch=source->source_epoch}; return true;
}
bool dj_link_browse_load_current(const dj_link_discovery_t *peers,
    const dj_link_incoming_load_t *r, uint32_t now)
{
    return r && r->deck<2 && (uint32_t)(now-r->received_ms)<3000 &&
        dj_link_discovery_number(peers,r->deck)==r->destination &&
        dj_link_discovery_source_current(peers,r->source,r->source_epoch);
}
