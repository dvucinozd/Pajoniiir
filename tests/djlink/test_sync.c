#include "dj_link_sync.h"
#include "djlink/sync.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned statuses,beats,requests,responses;
static uint32_t destination;
static bool send_packet(void *ctx,uint16_t port,uint32_t ip,const uint8_t *buf,size_t len)
{
    (void)ctx;destination=ip;
    djlink_status_t status;djlink_beat_t beat;
    djlink_handoff_req_t req;djlink_handoff_resp_t resp;
    if (port==DJLINK_PORT_STATUS) {
        assert(djlink_status_parse(buf,len,&status)==DJLINK_OK);
        assert(status.device_number==1 || status.device_number==2);
        assert(!status.source_slot && !status.rekordbox_id);++statuses;
    } else if (djlink_beat_parse(buf,len,&beat)==DJLINK_OK) {
        assert(beat.bpm100==12000);assert(beat.next_beat_ms<=500);++beats;
    } else if (djlink_handoff_req_parse(buf,len,&req)==DJLINK_OK) ++requests;
    else {assert(djlink_handoff_resp_parse(buf,len,&resp)==DJLINK_OK);++responses;}
    return true;
}
static dj_link_discovery_t discovery(void)
{
    dj_link_discovery_t d={0};d.phase=DJ_LINK_ACTIVE;d.epoch=10;d.ip=0x0a000001;
    d.numbers[0]=1;d.numbers[1]=2;
    dj_link_peer_t *p=&d.peers[0];p->present=p->has_status=p->has_beat=true;
    p->number=3;p->ip=0x0a000003;p->source_epoch=30;
    p->status.master_meaningful=1;p->status.flags=DJLINK_FLAG_MASTER|DJLINK_FLAG_PLAYING;
    p->beat.bpm100=12000;p->beat.pitch_raw=0x100000;p->beat.beat_in_bar=1;p->beat_rx_ms=1000;
    return d;
}
static void test_sticky_clock_loss_reorder_epoch(void)
{
    dj_link_discovery_t d=discovery();dj_link_sync_t s;dj_link_sync_init(&s);
    dj_link_local_clock_t local[2]={{0}};
    dj_link_sync_tick(&s,&d,local,1000,NULL,NULL);
    assert(s.clock.valid && s.peer==3 && s.clock.source_epoch==30);
    d.peers[0].beat_rx_ms=1503;d.peers[0].beat.beat_in_bar=2;
    dj_link_sync_tick(&s,&d,local,1503,NULL,NULL);assert(s.clock.anchor_ms==1503);
    d.peers[0].beat_rx_ms=1550;d.peers[0].beat.beat_in_bar=1;
    dj_link_sync_tick(&s,&d,local,1550,NULL,NULL);
    assert(s.rejected_beats && s.clock.anchor_ms==1503 && s.clock.beat_in_bar==2);
    d.peers[0].beat_rx_ms=2500;d.peers[0].beat.beat_in_bar=4; /* one packet lost */
    dj_link_sync_tick(&s,&d,local,2500,NULL,NULL);assert(s.clock.anchor_ms==2500);
    dj_link_sync_tick(&s,&d,local,3800,NULL,NULL);assert(!s.clock.valid);
    d.peers[1]=d.peers[0];d.peers[1].number=4;d.peers[1].source_epoch=40;
    d.peers[1].beat_rx_ms=3800;d.peers[0].present=false;
    dj_link_sync_tick(&s,&d,local,3800,NULL,NULL);assert(!s.clock.valid && s.peer==3);
    d.peers[0].present=true;d.peers[0].source_epoch=31; /* same locator, new media */
    dj_link_sync_tick(&s,&d,local,3800,NULL,NULL);assert(!s.clock.valid && s.peer_epoch==30);
    dj_link_sync_follow(&s);dj_link_sync_tick(&s,&d,local,3800,NULL,NULL);
    assert(!s.bound); /* two simultaneous masters do not select arbitrarily */
    d.peers[0].status.flags=0;
    dj_link_sync_tick(&s,&d,local,3800,NULL,NULL);assert(s.peer==4 && s.clock.valid);
    d.phase=DJ_LINK_WAIT_IP;dj_link_sync_tick(&s,&d,local,4000,NULL,NULL);
    assert(!s.clock.valid);
    d.phase=DJ_LINK_ACTIVE;d.epoch++;
    d.peers[1].source_epoch++;dj_link_sync_tick(&s,&d,local,4100,NULL,NULL);
    assert(!s.clock.valid); /* reconnect needs explicit user engage */
}
static void test_handoff_and_publisher(void)
{
    statuses=beats=requests=responses=0;
    dj_link_discovery_t d=discovery();dj_link_sync_t s;dj_link_sync_init(&s);
    dj_link_local_clock_t local[2]={
        {.loaded=true,.playing=true,.grid=true,.session=1,.bar=0,.bpm=120},
        {.loaded=true,.playing=true,.grid=true,.session=2,.bar=2,.bpm=120}};
    dj_link_sync_tick(&s,&d,local,1000,send_packet,NULL);
    assert(statuses==2 && beats==2 && s.clock.valid);
    dj_link_sync_tick(&s,&d,local,1040,send_packet,NULL);assert(statuses==2 && beats==2);
    assert(dj_link_sync_master(&s,&d,0,1040,send_packet,NULL));
    assert(requests==1 && s.pending_deck==0 && s.local_master==-1 && destination==d.peers[0].ip);
    uint8_t buf[DJLINK_MAX_PACKET];djlink_handoff_resp_t resp={.name="CDJ",.requester_number=3};
    int n=djlink_handoff_resp_build(&resp,buf,sizeof(buf));
    assert(!dj_link_sync_ingest(&s,&d,0x0a000004,buf,(size_t)n,1080,send_packet,NULL));
    assert(s.local_master==-1);
    assert(dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1080,send_packet,NULL));
    assert(s.local_master==0 && s.pending_deck==-1);
    dj_link_sync_tick(&s,&d,local,1100,NULL,NULL);assert(s.clock.valid && s.clock.player==1);
    assert(!dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1100,send_packet,NULL)); /* duplicate */
    djlink_handoff_req_t req={.name="CDJ",.requester_number=3};
    n=djlink_handoff_req_build(&req,buf,sizeof(buf));
    d.peers[0].status.flags=DJLINK_FLAG_PLAYING;
    assert(dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1120,send_packet,NULL));
    assert(s.yield_number==3 && s.local_master==0 && responses==1);
    uint32_t deadline=s.deadline_ms;
    assert(dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1130,send_packet,NULL));
    assert(responses==2 && s.deadline_ms==deadline);
    dj_link_sync_tick(&s,&d,local,1140,NULL,NULL);assert(s.local_master==0);
    d.peers[0].status.flags|=DJLINK_FLAG_MASTER;
    dj_link_sync_tick(&s,&d,local,1160,NULL,NULL);assert(s.local_master==-1 && s.peer==3 && !s.yield_number);
    assert(dj_link_sync_master(&s,&d,1,1200,send_packet,NULL));
    d.peers[0].source_epoch++; /* response from a swapped source */
    resp.requester_number=3;n=djlink_handoff_resp_build(&resp,buf,sizeof(buf));
    assert(!dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1240,send_packet,NULL));
    dj_link_sync_tick(&s,&d,local,4200,NULL,NULL);
    assert(s.pending_deck==-1 && s.handoff_timeouts==1 && s.local_master==-1);
    /* Explicit local master works without a remote authority, then yields
     * only after a valid request, retaining authority if handoff times out. */
    d.peers[0].status.flags=0;
    dj_link_sync_follow(&s);dj_link_sync_tick(&s,&d,local,4300,NULL,NULL);
    assert(dj_link_sync_master(&s,&d,1,4300,send_packet,NULL));assert(s.local_master==1);
    local[1].hold=true;dj_link_sync_tick(&s,&d,local,4340,NULL,NULL);assert(!s.clock.valid);
    local[1].hold=false;n=djlink_handoff_req_build(&req,buf,sizeof(buf));
    assert(dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,4400,send_packet,NULL));
    dj_link_sync_tick(&s,&d,local,7400,NULL,NULL);
    assert(s.local_master==1 && !s.yield_number && s.handoff_timeouts==2);
    d.phase=DJ_LINK_OBSERVER;unsigned old=beats+statuses;
    dj_link_sync_tick(&s,&d,local,7500,send_packet,NULL);assert(beats+statuses==old && !s.clock.valid);
}
static void test_announced_remote_handoff(void)
{
    dj_link_discovery_t d=discovery();dj_link_sync_t s;dj_link_sync_init(&s);
    dj_link_local_clock_t local[2]={{0}};
    dj_link_sync_tick(&s,&d,local,1000,NULL,NULL);
    d.peers[1]=d.peers[0];d.peers[1].number=4;d.peers[1].source_epoch=40;d.peers[1].ip++;
    d.peers[1].status.flags=DJLINK_FLAG_PLAYING;
    d.peers[0].status.master_handoff=4;
    dj_link_sync_tick(&s,&d,local,1040,NULL,NULL);
    assert(s.peer==3 && s.remote_yield_number==4);
    d.peers[0].status.flags=DJLINK_FLAG_PLAYING;d.peers[1].status.flags|=DJLINK_FLAG_MASTER;
    dj_link_sync_tick(&s,&d,local,1080,NULL,NULL);
    assert(s.peer==4 && s.peer_epoch==40 && s.clock.valid && s.master_epoch==2);
    d.peers[1].status.master_handoff=3;
    dj_link_sync_tick(&s,&d,local,1120,NULL,NULL);
    d.peers[0].source_epoch++;d.peers[0].status.flags|=DJLINK_FLAG_MASTER;
    dj_link_sync_tick(&s,&d,local,1160,NULL,NULL);
    assert(s.peer==4 && s.peer_epoch==40); /* swapped target cannot accept */
    dj_link_sync_tick(&s,&d,local,5000,NULL,NULL);
    assert(s.peer==4 && !s.remote_yield_number); /* TTL cannot rebind a swapped target */
}

static void test_peer_inventory_does_not_revoke_claim(void)
{
    dj_link_discovery_t d=discovery();dj_link_sync_t s;dj_link_sync_init(&s);
    dj_link_local_clock_t local[2]={{0}};
    dj_link_sync_tick(&s,&d,local,1000,NULL,NULL);
    assert(dj_link_sync_master(&s,&d,0,1040,send_packet,NULL));
    ++d.epoch; /* unrelated peer arrival/expiry */
    dj_link_sync_tick(&s,&d,local,1080,NULL,NULL);
    assert(s.pending_deck==0 && s.peer==3 && s.clock.valid);
    uint8_t buf[DJLINK_MAX_PACKET];
    djlink_handoff_resp_t resp={.name="CDJ",.requester_number=3};
    int n=djlink_handoff_resp_build(&resp,buf,sizeof(buf));
    assert(dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1100,send_packet,NULL));
    ++d.epoch;dj_link_sync_tick(&s,&d,local,1120,NULL,NULL);
    assert(s.local_master==0);
    ++d.ip; /* reject commands immediately, even before the next worker tick */
    assert(!dj_link_sync_master(&s,&d,1,1140,send_packet,NULL));
    assert(!dj_link_sync_ingest(&s,&d,d.peers[0].ip,buf,(size_t)n,1140,send_packet,NULL));
    dj_link_sync_tick(&s,&d,local,1160,NULL,NULL);
    assert(s.local_master==-1 && s.pending_deck==-1 && !s.clock.valid);
    d.phase=DJ_LINK_OBSERVER;
    assert(!dj_link_sync_master(&s,&d,0,1180,send_packet,NULL));
}

int main(void)
{
    test_sticky_clock_loss_reorder_epoch();test_handoff_and_publisher();
    test_announced_remote_handoff();
    test_peer_inventory_does_not_revoke_claim();
    puts("djlink sync: loss/reorder/epoch/handoff/status/beat tests passed");return 0;
}
