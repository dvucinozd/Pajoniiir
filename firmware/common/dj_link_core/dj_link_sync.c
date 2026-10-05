#include "dj_link_sync.h"
#include "djlink/sync.h"
#include <math.h>
#include <string.h>

#define HANDOFF_MS 3000u
#define STATUS_MS 200u
static bool same_identity(const dj_link_sync_t *s, const dj_link_discovery_t *d)
{
    return s->own_active && d->phase==DJ_LINK_ACTIVE && s->own_ip==d->ip &&
        !memcmp(s->own_numbers,d->numbers,sizeof(s->own_numbers)) &&
        !memcmp(s->own_mac,d->mac,sizeof(s->own_mac));
}
static const dj_link_peer_t *peer(const dj_link_discovery_t *d, uint8_t n, uint64_t epoch)
{
    if (!d) return NULL;
    for (unsigned i=0;i<DJ_LINK_PEERS;++i) {
        const dj_link_peer_t *p=&d->peers[i];
        if (p->present && p->number==n && (!epoch || p->source_epoch==epoch)) return p;
    }
    return NULL;
}
static bool is_master(const dj_link_peer_t *p)
{
    return p && p->present && p->has_status && p->status.master_meaningful &&
        (p->status.flags & DJLINK_FLAG_MASTER);
}
static void select_peer(dj_link_sync_t *s, const dj_link_peer_t *p)
{
    s->bound=true;s->peer=p->number;s->peer_epoch=p->source_epoch;
    s->local_master=-1;s->have_beat=false;s->clock.valid=false;++s->master_epoch;
    s->remote_seen_number=0;
}
void dj_link_sync_init(dj_link_sync_t *s)
{
    memset(s,0,sizeof(*s));s->local_master=s->pending_deck=-1;
}
void dj_link_sync_follow(dj_link_sync_t *s)
{
    /* Do not abandon an acknowledged outgoing handoff halfway through. */
    if (!s || s->yield_number || s->pending_deck>=0 || s->local_master>=0) return;
    s->bound=false;s->peer=0;s->clock.valid=false;s->have_beat=false;
    s->remote_yield_number=0;
    s->remote_seen_number=0;
}
bool dj_link_sync_master(dj_link_sync_t *s, const dj_link_discovery_t *d,
    unsigned deck, uint32_t now, dj_link_sync_send_t send, void *ctx)
{
    if (!s || !d || !same_identity(s,d) || deck>1 || !dj_link_discovery_number(d,deck) ||
        s->pending_deck>=0 || s->yield_number) return false;
    if (s->local_master>=0) {
        s->local_master=(int8_t)deck;++s->master_epoch;s->have_beat=false;return true;
    }
    const dj_link_peer_t *p=peer(d,s->peer,s->peer_epoch);
    if (!p) {
        unsigned masters=0;
        for (unsigned i=0;i<DJ_LINK_PEERS;++i) if (is_master(&d->peers[i])) {
            p=&d->peers[i];++masters;
        }
        if (masters>1) return false;
        if (!masters) {
            s->bound=true;s->peer=0;s->local_master=(int8_t)deck;
            ++s->master_epoch;s->have_beat=false;return true;
        }
        select_peer(s,p);
    }
    if (!is_master(p)) return false;
    uint8_t buf[DJLINK_HANDOFF_REQ_PACKET_LEN];
    djlink_handoff_req_t req={.name="Pajoniiir",.requester_number=d->numbers[deck]};
    int n=djlink_handoff_req_build(&req,buf,sizeof(buf));
    if (n<=0 || !send || !send(ctx,DJLINK_PORT_BEAT,p->ip,buf,(size_t)n)) return false;
    s->pending_deck=(int8_t)deck;s->pending_number=d->numbers[deck];
    s->deadline_ms=now+HANDOFF_MS;return true;
}
bool dj_link_sync_ingest(dj_link_sync_t *s, const dj_link_discovery_t *d,
    uint32_t ip, const uint8_t *packet, size_t len, uint32_t now,
    dj_link_sync_send_t send, void *ctx)
{
    if (!s || !d || !same_identity(s,d) || !packet || len<11) return false;
    const dj_link_peer_t *p=peer(d,s->peer,s->peer_epoch);
    djlink_handoff_resp_t resp;
    if (djlink_handoff_resp_parse(packet,len,&resp)==DJLINK_OK) {
        if (s->pending_deck>=0 && p && p->ip==ip &&
            resp.requester_number==p->number && (int32_t)(now-s->deadline_ms)<0 &&
            dj_link_discovery_number(d,(unsigned)s->pending_deck)==s->pending_number) {
            s->local_master=s->pending_deck;s->pending_deck=-1;s->peer=0;
            ++s->master_epoch;s->clock.valid=false;s->have_beat=false;return true;
        }
        ++s->rejected_controls;return false;
    }
    djlink_handoff_req_t req;
    if (djlink_handoff_req_parse(packet,len,&req)==DJLINK_OK) {
        p=peer(d,req.requester_number,0);
        if (s->local_master<0 || !p || p->ip!=ip || (s->yield_number &&
            (s->yield_number!=p->number || s->peer_epoch!=p->source_epoch))) {
            ++s->rejected_controls;return false;
        }
        uint8_t buf[DJLINK_HANDOFF_RESP_PACKET_LEN];
        djlink_handoff_resp_t r={.name="Pajoniiir",.requester_number=d->numbers[(unsigned)s->local_master]};
        int n=djlink_handoff_resp_build(&r,buf,sizeof(buf));
        if (n<=0 || !send || !send(ctx,DJLINK_PORT_BEAT,ip,buf,(size_t)n)) return false;
        if (!s->yield_number) {
            s->yield_number=p->number;s->peer_epoch=p->source_epoch;s->deadline_ms=now+HANDOFF_MS;
        }
        return true; /* idempotent reply to a retried request, same deadline */
    }
    return false;
}
static void remote_clock(dj_link_sync_t *s, const dj_link_peer_t *p, uint32_t now)
{
    s->clock.valid=false;
    if (!is_master(p) || !(p->status.flags & DJLINK_FLAG_PLAYING) || !p->has_beat) return;
    float bpm=djlink_effective_bpm(p->beat.bpm100,p->beat.pitch_raw);
    if (!isfinite(bpm) || bpm<20 || bpm>400 || p->beat.beat_in_bar<1 || p->beat.beat_in_bar>4) return;
    uint32_t period=(uint32_t)lroundf(60000000.0f/bpm);
    if (!s->have_beat || p->beat_rx_ms!=s->beat_rx_ms) {
        bool accept=true;
        if (s->have_beat) {
            int32_t elapsed=(int32_t)(p->beat_rx_ms-s->beat_rx_ms);
            float steps=(float)elapsed*1000.0f/(float)s->clock.period_us;
            unsigned rounded=(unsigned)fmaxf(0,lroundf(steps));
            uint8_t expected=(uint8_t)((s->clock.beat_in_bar-1+rounded)%4+1);
            /* Delayed/reordered/duplicate beat packets must not rewind phase.
             * After stale loss accept a fresh anchor, without seeking. */
            if (elapsed<=0 || (steps<DECK_NET_SYNC_STALE_BEATS &&
                (steps<0.5f || fabsf(steps-(float)rounded)>0.25f || expected!=p->beat.beat_in_bar))) accept=false;
        }
        if (accept) {
            s->clock.anchor_ms=p->beat_rx_ms;s->clock.period_us=period;
            s->clock.beat_in_bar=p->beat.beat_in_bar;s->beat_rx_ms=p->beat_rx_ms;s->have_beat=true;
        } else ++s->rejected_beats;
    }
    s->clock.player=p->number;s->clock.source_epoch=p->source_epoch;
    s->clock.master_epoch=s->master_epoch;
    s->clock.valid=s->have_beat && (uint32_t)(now-s->clock.anchor_ms)*1000.0f <=
        (float)s->clock.period_us*DECK_NET_SYNC_STALE_BEATS;
}
static void publish_decks(dj_link_sync_t *s, const dj_link_discovery_t *d,
    const dj_link_local_clock_t local[2], uint32_t now, dj_link_sync_send_t send, void *ctx)
{
    uint8_t buf[DJLINK_MAX_PACKET];
    bool status_due=!s->have_status || (uint32_t)(now-s->status_ms)>=STATUS_MS;
    for (unsigned deck=0;deck<2;++deck) {
        const dj_link_local_clock_t *l=&local[deck];uint8_t n=d->numbers[deck];
        bool valid=l->loaded && l->grid && isfinite(l->bar) && isfinite(l->bpm) &&
            isfinite(l->pitch) && l->bpm>=20 && l->bpm<=400 && fabsf(l->pitch)<=100 && l->bar>=0 && l->bar<4;
        uint8_t beat=valid?(uint8_t)(floorf(l->bar)+1):0;
        float effective=valid?l->bpm*(1+l->pitch/100):0;
        if (status_due) {
            djlink_status_t st={0};memcpy(st.name,"Pajoniiir",9);
            st.device_number=n;st.active=l->loaded;st.has_flag_bits=true;
            st.flags=(l->playing?DJLINK_FLAG_PLAYING:0) | (l->sync?DJLINK_FLAG_SYNC:0) |
                (s->local_master==(int8_t)deck?DJLINK_FLAG_MASTER:0);
            st.play_state=l->loaded?(l->playing?DJLINK_PLAY_PLAYING:DJLINK_PLAY_PAUSE):DJLINK_PLAY_NO_TRACK;
            st.bpm100=valid?(uint16_t)lroundf(l->bpm*100):0xffff;
            st.pitch_raw=djlink_pitch_percent_to_raw(valid?l->pitch:0);
            st.master_meaningful=true;
            st.master_handoff=s->local_master==(int8_t)deck && s->yield_number?s->yield_number:0xff;
            st.beat_in_bar=beat;
            int len=djlink_status_build(&st,++s->packet_counter[deck],buf,sizeof(buf));
            if (len>0) for (unsigned i=0;i<DJ_LINK_PEERS;++i) if (d->peers[i].present)
                (void)send(ctx,DJLINK_PORT_STATUS,d->peers[i].ip,buf,(size_t)len);
        }
        if (l->session!=s->local_session[deck]) {s->last_beat[deck]=0;s->local_session[deck]=l->session;}
        if (valid && l->playing && !l->hold && effective>0 && beat!=s->last_beat[deck]) {
            float period=60000/l->bpm; /* protocol fields assume 0% pitch */
            uint32_t next=(uint32_t)lroundf((1-(l->bar-floorf(l->bar)))*period);
            djlink_beat_t b={0};memcpy(b.name,"Pajoniiir",9);b.device_number=n;
            b.beat_in_bar=beat;b.bpm100=(uint16_t)lroundf(l->bpm*100);
            b.pitch_raw=djlink_pitch_percent_to_raw(l->pitch);
            b.next_beat_ms=next;b.second_beat_ms=next+(uint32_t)period;
            b.next_bar_ms=next+(4-beat)*(uint32_t)period;
            b.fourth_beat_ms=next+3*(uint32_t)period;
            b.second_bar_ms=b.next_bar_ms+4*(uint32_t)period;
            b.eighth_beat_ms=next+7*(uint32_t)period;
            int len=djlink_beat_build(&b,buf,sizeof(buf));
            if (len>0) (void)send(ctx,DJLINK_PORT_BEAT,0 /* Ethernet broadcast */,buf,(size_t)len);
        }
        s->last_beat[deck]=l->playing?beat:0;
    }
    if (status_due) {s->have_status=true;s->status_ms=now;}
}
void dj_link_sync_tick(dj_link_sync_t *s, const dj_link_discovery_t *d,
    const dj_link_local_clock_t local[2], uint32_t now, dj_link_sync_send_t send, void *ctx)
{
    if (!s || !d || !local) return;
    s->network_epoch=d->epoch; /* peer inventory changes are not our claim changes */
    if (!same_identity(s,d)) {
        /* Remember a lost remote binding so a reconnect cannot choose another
         * master until explicit engage. In-flight handoffs are invalidated. */
        s->clock.valid=false;s->have_beat=false;s->pending_deck=-1;s->yield_number=0;
        s->remote_yield_number=0;
        s->local_master=-1;s->enabled=d->phase!=DJ_LINK_OFF;
        s->own_active=d->phase==DJ_LINK_ACTIVE;s->own_ip=d->ip;
        memcpy(s->own_numbers,d->numbers,sizeof(s->own_numbers));
        memcpy(s->own_mac,d->mac,sizeof(s->own_mac));
        s->have_status=false;memset(s->last_beat,0,sizeof(s->last_beat));
        if (d->phase!=DJ_LINK_ACTIVE) return;
    }
    s->enabled=true;
    if ((s->pending_deck>=0 || s->yield_number) && (int32_t)(now-s->deadline_ms)>=0) {
        s->pending_deck=-1;s->yield_number=0;++s->handoff_timeouts;
    }
    if (s->yield_number) {
        const dj_link_peer_t *p=peer(d,s->yield_number,s->peer_epoch);
        if (is_master(p)) {select_peer(s,p);s->yield_number=0;}
    }
    /* Remote -> remote handoff must be announced by the selected authority,
     * then confirmed by that exact destination/source epoch within the TTL. */
    if (s->bound && s->local_master<0 && s->pending_deck<0) {
        const dj_link_peer_t *authority=peer(d,s->peer,s->peer_epoch);
        if (s->remote_yield_number && (!authority ||
            (int32_t)(now-s->remote_deadline_ms)>=0)) s->remote_yield_number=0;
        if (!s->remote_yield_number && is_master(authority) &&
            authority->status.master_handoff!=s->remote_seen_number) {
            uint8_t target=authority->status.master_handoff;
            s->remote_seen_number=target;
            const dj_link_peer_t *next=target!=s->peer?peer(d,target,0):NULL;
            if (next && target>=1 && target<=6) {
                s->remote_yield_number=target;s->remote_yield_epoch=next->source_epoch;
                s->remote_deadline_ms=now+HANDOFF_MS;
            }
        }
        if (s->remote_yield_number) {
            const dj_link_peer_t *next=peer(d,s->remote_yield_number,s->remote_yield_epoch);
            if (is_master(next)) {select_peer(s,next);s->remote_yield_number=0;}
        }
    }
    if (!s->bound) {
        const dj_link_peer_t *found=NULL;unsigned count=0;
        for (unsigned i=0;i<DJ_LINK_PEERS;++i) if (is_master(&d->peers[i])) {found=&d->peers[i];++count;}
        if (count==1) select_peer(s,found);
    }
    if (s->local_master>=0) {
        unsigned deck=(unsigned)s->local_master;const dj_link_local_clock_t *l=&local[deck];
        float bpm=l->bpm*(1+l->pitch/100);
        s->clock.valid=l->loaded && l->playing && !l->hold && l->grid && isfinite(bpm) &&
            bpm>=20 && bpm<=400 && isfinite(l->bar) && l->bar>=0 && l->bar<4;
        if (s->clock.valid) {
            s->clock.period_us=(uint32_t)lroundf(60000000/bpm);
            s->clock.anchor_ms=now-(uint32_t)lroundf((l->bar-floorf(l->bar))*s->clock.period_us/1000);
            s->clock.beat_in_bar=(uint8_t)(floorf(l->bar)+1);
            s->clock.player=d->numbers[deck];s->clock.source_epoch=l->session;
            s->clock.master_epoch=s->master_epoch;
        }
    } else remote_clock(s,peer(d,s->peer,s->peer_epoch),now);
    if (send) publish_decks(s,d,local,now,send,ctx);
}
