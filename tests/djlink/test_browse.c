#include "dj_link_browse.h"
#include "djlink/mixer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static dj_link_peer_track_t rows[DJ_LINK_DB_TRACK_LIMIT];
static unsigned closes;
static int connect_peer(void *ctx,uint32_t ip,uint16_t port)
{ (void)ctx; assert(ip==0xc0a80102 && port==12523); return 0; }
static int send_peer(void *ctx,const uint8_t *bytes,size_t len)
{ (void)ctx; (void)bytes; (void)len; return 0; }
static void close_peer(void *ctx) { (void)ctx; ++closes; }
int main(void)
{
    dj_link_discovery_t peers={.phase=DJ_LINK_ACTIVE,.numbers={3,4}};
    peers.peers[0]=(dj_link_peer_t){.present=true,.number=2,.ip=0xc0a80102,.source_epoch=9};
    dj_link_db_io_t transport={.connect=connect_peer,.send=send_peer,.close=close_peer};
    dj_link_browse_t b; dj_link_browse_init(&b,rows,&transport);
    assert(!dj_link_browse_start(&b,&peers,2,8,DJLINK_SLOT_USB,DJ_LINK_DB_MENU_ALL_TRACKS,0,0));
    assert(dj_link_browse_start(&b,&peers,2,9,DJLINK_SLOT_USB,DJ_LINK_DB_MENU_ALL_TRACKS,0,0));
    b.db.io.list_begin(b.db.io.ctx,2023);
    dj_link_browse_status_t status; dj_link_peer_track_t page[8];
    for (uint32_t i=0;i<2000;++i) {
        dj_link_peer_track_t t={.rekordbox_id=i+1}; snprintf(t.title,sizeof(t.title),"track %u",i+1);
        b.db.io.track(b.db.io.ctx,i,&t,false);
    }
    assert(dj_link_browse_page(&b,0,&status,page)==0 && status.received==2000 && status.count==0);
    b.db.list_done=true; b.db.phase=DJ_LINK_DB_READY;
    dj_link_browse_poll(&b,&peers,1);
    assert(dj_link_browse_page(&b,1998,&status,page)==2);
    assert(status.total==2023 && status.count==2000 && page[1].rekordbox_id==2000);
    rows[1999].title[0]='X';
    assert(!strcmp(page[1].title,"track 2000")); /* caller owns the page */
    /* Copy survives cache replacement, and only the requested visible window
     * requests detail. No permanent ID is inferred from these runtime IDs. */
    uint32_t index,id;
    assert(b.db.io.next_detail(b.db.io.ctx,&index,&id) && index==1998 && id==1999);
    dj_link_peer_track_t detail=rows[1998]; detail.has_detail=true; strcpy(detail.artist,"artist");
    b.db.io.track(b.db.io.ctx,1998,&detail,true);
    assert(b.db.io.next_detail(b.db.io.ctx,&index,&id) && index==1999);
    detail.rekordbox_id=44; b.db.io.track(b.db.io.ctx,1999,&detail,true);
    assert(rows[1999].rekordbox_id==2000 && !rows[1999].has_detail);
    ++peers.peers[0].source_epoch;
    assert(!dj_link_browse_validate(&b,&peers));
    assert(dj_link_browse_page(&b,0,&status,page)==0 && status.state==DJ_LINK_BROWSE_UNAVAILABLE);
    assert(page[0].rekordbox_id==0 && closes>0);
    assert(dj_link_browse_start(&b,&peers,2,10,DJLINK_SLOT_USB,DJ_LINK_DB_MENU_FOLDER,55,2));
    assert(b.db.menu==DJ_LINK_DB_MENU_FOLDER && b.db.menu_id==55 && !b.status.count);
    uint64_t old=b.db.connection_epoch;
    dj_link_browse_cancel(&b);
    uint8_t junk[32]={0}; dj_link_db_on_data(&b.db,old,junk,sizeof(junk),3);
    assert(b.db.phase==DJ_LINK_DB_IDLE && b.status.state==DJ_LINK_BROWSE_IDLE);
    assert(dj_link_browse_start(&b,&peers,2,10,DJLINK_SLOT_USB,DJ_LINK_DB_MENU_PLAYLIST,5,4));
    b.db.phase=DJ_LINK_DB_FAILED; strcpy(b.db.error,"PARTIAL LIST");
    dj_link_browse_poll(&b,&peers,5); assert(b.status.state==DJ_LINK_BROWSE_FAILED && !b.status.count);
    /* Incoming commands require a live authenticated-by-address sender, a
     * known source epoch and one of our two distinct claimed destinations. */
    djlink_load_track_t load={.name="peer",.sender_number=2,.target_player=2,
        .slot=DJLINK_SLOT_USB,.rekordbox_id=7};
    uint8_t packet[DJLINK_LOAD_TRACK_PACKET_LEN];
    assert(djlink_load_track_build(&load,packet,sizeof(packet))>0); packet[0x40]=3;
    dj_link_incoming_load_t command;
    assert(dj_link_browse_parse_load(&peers,0xc0a80102,packet,sizeof(packet),&command));
    assert(command.deck==1 && command.source_epoch==10 && command.track_id==7);
    assert(dj_link_browse_load_current(&peers,&command,2999));
    assert(!dj_link_browse_load_current(&peers,&command,3000));
    ++peers.peers[0].source_epoch;
    assert(!dj_link_browse_load_current(&peers,&command,1));
    --peers.peers[0].source_epoch;
    assert(!dj_link_browse_parse_load(&peers,0xc0a80103,packet,sizeof(packet),&command));
    assert(!dj_link_browse_parse_load(&peers,0xc0a80102,packet,0x40,&command));
    packet[0x40]=5; assert(!dj_link_browse_parse_load(&peers,0xc0a80102,packet,sizeof(packet),&command));
    peers.phase=DJ_LINK_OBSERVER;
    assert(!dj_link_browse_parse_load(&peers,0xc0a80102,packet,sizeof(packet),&command));
    puts("PASS browse ownership, cap, detail window, stale epoch, cancel, partial failure, incoming load routing");
    return 0;
}
