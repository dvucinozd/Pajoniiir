/* Claim sequence adapted from kayrozen/Pajoniiir 428b97dd dj_link_session.
 * MIT attribution and exact provenance: README.md and LICENSE.
 * No transport, storage, UI, library advertising or playback actions here. */
#include "dj_link_discovery.h"
#include "djlink/claim.h"
#include <string.h>

static bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
static void advance_epoch(dj_link_discovery_t *s) { ++s->epoch; }

void dj_link_discovery_init(dj_link_discovery_t *s)
{
    if (s) { memset(s, 0, sizeof(*s)); s->epoch = 1; }
}
void dj_link_discovery_configure(dj_link_discovery_t *s, bool enabled,
                                uint32_t ip, const uint8_t mac[6], uint32_t now)
{
    if (!s || !mac) return;
    const bool was_enabled = s->phase != DJ_LINK_OFF;
    if (was_enabled == enabled && s->ip == ip && !memcmp(s->mac, mac, 6)) return;
    advance_epoch(s);
    memset(s->peers, 0, sizeof(s->peers));
    memset(s->busy, 0, sizeof(s->busy));
    memset(s->numbers, 0, sizeof(s->numbers));
    memcpy(s->mac, mac, 6); s->ip = ip;
    s->phase = !enabled ? DJ_LINK_OFF : !ip ? DJ_LINK_WAIT_IP : DJ_LINK_ANNOUNCE;
    s->reason = DJ_LINK_REASON_NONE;
    s->iteration = 1; s->sent_mask = 0; s->next_ms = now;
}
void dj_link_discovery_tick(dj_link_discovery_t *s, uint32_t now)
{
    if (!s) return;
    for (unsigned i = 0; i < 6; ++i)
        if (s->busy[i] && (uint32_t)(now - s->busy_ms[i]) >= DJ_LINK_TIMEOUT_MS)
            s->busy[i] = false;
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i) {
        dj_link_peer_t *p = &s->peers[i];
        if (p->present && (uint32_t)(now - p->last_ms) >= DJ_LINK_TIMEOUT_MS) {
            memset(p, 0, sizeof(*p)); ++s->peer_expirations; advance_epoch(s);
        }
    }
}
static bool select_pair(dj_link_discovery_t *s)
{
    static const uint8_t preference[][2] = {{3,4}, {2,3}, {1,2}, {5,6}, {4,5}};
    for (unsigned i = 0; i < sizeof(preference)/sizeof(preference[0]); ++i) {
        uint8_t a = preference[i][0], b = preference[i][1];
        if (!s->busy[a-1] && !s->busy[b-1]) {
            s->numbers[0] = a; s->numbers[1] = b; return true;
        }
    }
    uint8_t a = 0;
    for (uint8_t n = 1; n <= 6; ++n) {
        if (s->busy[n-1]) continue;
        if (!a) a = n;
        else { s->numbers[0] = a; s->numbers[1] = n; return true; }
    }
    s->numbers[0] = s->numbers[1] = 0; return false;
}
static void note_number(dj_link_discovery_t *s, uint8_t number, uint32_t now)
{
    if (number < 1 || number > 6) return;
    s->busy[number-1] = true; s->busy_ms[number-1] = now;
    if ((s->phase == DJ_LINK_ACTIVE || s->phase == DJ_LINK_CLAIM_IP ||
         s->phase == DJ_LINK_CLAIM_FINAL) &&
        (s->numbers[0] == number || s->numbers[1] == number)) {
        ++s->conflicts; advance_epoch(s);
        s->numbers[0] = s->numbers[1] = 0;
        s->phase = DJ_LINK_OBSERVER; s->next_ms = now;
        s->sent_mask = 0; s->iteration = 1;
    }
}
static dj_link_peer_t *peer(dj_link_discovery_t *s, uint8_t number, uint32_t ip,
                            const uint8_t *name, const uint8_t *mac, uint32_t now)
{
    dj_link_peer_t *slot = NULL, *oldest = &s->peers[0];
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i) {
        dj_link_peer_t *p = &s->peers[i];
        if (p->present && p->number == number) { slot = p; break; }
        if (!p->present && !slot) slot = p;
        if ((uint32_t)(now - p->last_ms) > (uint32_t)(now - oldest->last_ms)) oldest = p;
    }
    if (!slot) slot = oldest;
    if (!slot->present || slot->number != number || slot->ip != ip ||
        (mac && slot->has_mac && memcmp(slot->mac, mac, 6))) {
        memset(slot, 0, sizeof(*slot)); advance_epoch(s);
        slot->source_epoch = s->epoch;
        slot->present = true; slot->number = number; slot->ip = ip;
    }
    slot->last_ms = now;
    djlink_name_to_str(name, slot->name);
    if (mac) { memcpy(slot->mac, mac, 6); slot->has_mac = true; }
    return slot;
}
bool dj_link_discovery_ingest(dj_link_discovery_t *s, uint16_t port,
                              const uint8_t *buf, size_t len, uint32_t ip, uint32_t now)
{
    if (!s) return false;
    if (s->phase == DJ_LINK_OFF || s->phase == DJ_LINK_WAIT_IP || !ip || ip == s->ip ||
        len > DJLINK_MAX_PACKET || !djlink_packet_is_valid(buf, len)) goto dropped;
    dj_link_discovery_tick(s, now);
    const uint8_t type = buf[0x0a];
    if (port == DJLINK_PORT_DISCOVERY) {
        if (type == DJLINK_TYPE_KEEPALIVE) {
            if (len < DJLINK_KEEPALIVE_PACKET_LEN || !memcmp(buf+0x26, s->mac, 6)) goto dropped;
            uint8_t number = buf[0x24], device_type = buf[0x21];
            if (!number || (device_type != 3 &&
                (device_type != DJLINK_DEVICE_TYPE_CDJ || number > 6))) goto dropped;
            dj_link_peer_t *p = peer(s, number, ip, buf+0x0c, buf+0x26, now);
            p->device_type = device_type; note_number(s, number, now);
        } else if (type == 0x02 || type == 0x04 || type == 0x08) {
            size_t required = type == 0x02 ? DJLINK_CLAIM_IP_PACKET_LEN :
                              type == 0x04 ? DJLINK_CLAIM_FINAL_PACKET_LEN : 0x25;
            if (len < required) goto dropped;
            if (type == 0x02 && !memcmp(buf+0x28, s->mac, 6)) goto dropped;
            uint8_t number = buf[type == 0x02 ? 0x2e : 0x24];
            if (number < 1 || number > 6) goto dropped;
            note_number(s, number, now);
        } else goto dropped;
    } else if (port == DJLINK_PORT_BEAT && type == DJLINK_TYPE_BEAT) {
        djlink_beat_t b;
        if (djlink_beat_parse(buf, len, &b) != DJLINK_OK ||
            b.device_number < 1 || b.device_number > 6 || b.beat_in_bar > 4) goto dropped;
        dj_link_peer_t *p = peer(s, b.device_number, ip, b.name, NULL, now);
        p->beat = b; p->has_beat = true; p->beat_rx_ms = now;
        note_number(s, b.device_number, now);
    } else if (port == DJLINK_PORT_BEAT && type == DJLINK_TYPE_ABS_POSITION) {
        djlink_position_t pos;
        if (djlink_position_parse(buf, len, &pos) != DJLINK_OK ||
            pos.device_number < 1 || pos.device_number > 6) goto dropped;
        dj_link_peer_t *p = peer(s, pos.device_number, ip, pos.name, NULL, now);
        p->position = pos; p->has_position = true; p->position_rx_ms = now;
        note_number(s, pos.device_number, now);
    } else if (port == DJLINK_PORT_STATUS && type == DJLINK_TYPE_CDJ_STATUS) {
        djlink_status_t st;
        if (djlink_status_parse(buf, len, &st) != DJLINK_OK ||
            st.device_number < 1 || st.device_number > 6) goto dropped;
        dj_link_peer_t *p = peer(s, st.device_number, ip, buf+0x0b, NULL, now);
        p->status = st; p->has_status = true; note_number(s, st.device_number, now);
    } else goto dropped;
    ++s->accepted; return true;
dropped:
    ++s->dropped; return false;
}
int dj_link_discovery_poll(dj_link_discovery_t *s, uint32_t now,
                           uint8_t *out, size_t cap, unsigned *deck)
{
    if (!s || !out || !deck) return 0;
    dj_link_discovery_tick(s, now);
    if (!due(now, s->next_ms) || s->phase == DJ_LINK_OFF || s->phase == DJ_LINK_WAIT_IP) return 0;
    if (s->phase == DJ_LINK_OBSERVER) {
        if (!select_pair(s)) {
            s->reason = DJ_LINK_REASON_NO_PAIR; s->next_ms = now + DJ_LINK_KEEPALIVE_MS; return 0;
        }
        s->phase = DJ_LINK_CLAIM_IP; s->reason = DJ_LINK_REASON_NONE;
        s->sent_mask = 0; s->iteration = 1;
    }
    unsigned d = s->sent_mask & 1 ? 1 : 0;
    const char *name = d ? "Pajoniiir D2" : "Pajoniiir D1";
    int n = 0;
    switch (s->phase) {
    case DJ_LINK_ANNOUNCE: {
        djlink_announce_t a = {.name=name, .device_type=DJLINK_DEVICE_TYPE_CDJ, .payload_byte=1};
        n = djlink_announce_build(&a, out, cap); break;
    }
    case DJ_LINK_CLAIM_MAC: {
        djlink_claim_mac_t c = {.name=name, .device_type=DJLINK_DEVICE_TYPE_CDJ, .iteration=s->iteration};
        memcpy(c.mac, s->mac, 6); n = djlink_claim_mac_build(&c, out, cap); break;
    }
    case DJ_LINK_CLAIM_IP: {
        djlink_claim_ip_t c = {.name=name, .device_type=DJLINK_DEVICE_TYPE_CDJ,
            .iteration=s->iteration, .device_number=s->numbers[d], .ip=s->ip, .auto_assign=1};
        memcpy(c.mac, s->mac, 6); n = djlink_claim_ip_build(&c, out, cap); break;
    }
    case DJ_LINK_CLAIM_FINAL: {
        djlink_claim_final_t c = {.name=name, .device_type=DJLINK_DEVICE_TYPE_CDJ,
            .iteration=s->iteration, .device_number=s->numbers[d]};
        n = djlink_claim_final_build(&c, out, cap); break;
    }
    case DJ_LINK_ACTIVE: {
        uint8_t count = 2;
        for (unsigned i = 0; i < DJ_LINK_PEERS; ++i) count += s->peers[i].present;
        djlink_keepalive_t ka = {.name=name, .device_number=s->numbers[d], .ip=s->ip,
            .peer_count=count, .startup_flags=count > 2 ? 1 : 2};
        memcpy(ka.mac, s->mac, 6); n = djlink_keepalive_build(&ka, out, cap);
        if (n > 0 && s->numbers[d] > 4) out[0x35] = 0x64;
        break;
    }
    default: return 0;
    }
    if (n <= 0) return 0;
    *deck = d; s->sent_mask |= (uint8_t)(1u << d);
    if (s->sent_mask != 3) return n;
    s->sent_mask = 0;
    if (s->phase == DJ_LINK_ACTIVE) s->next_ms = now + DJ_LINK_KEEPALIVE_MS;
    else {
        s->next_ms = now + DJ_LINK_CLAIM_MS;
        if (++s->iteration > 3) {
            s->iteration = 1;
            if (s->phase == DJ_LINK_CLAIM_MAC) {
                s->phase = select_pair(s) ? DJ_LINK_CLAIM_IP : DJ_LINK_OBSERVER;
                s->reason = s->phase == DJ_LINK_OBSERVER ? DJ_LINK_REASON_NO_PAIR : DJ_LINK_REASON_NONE;
            } else if (s->phase == DJ_LINK_CLAIM_FINAL) {
                s->phase = DJ_LINK_ACTIVE; s->next_ms = now;
            } else s->phase = (dj_link_phase_t)(s->phase + 1);
        }
    }
    return n;
}
uint8_t dj_link_discovery_number(const dj_link_discovery_t *s, unsigned deck)
{ return s && s->phase == DJ_LINK_ACTIVE && deck < 2 ? s->numbers[deck] : 0; }
bool dj_link_discovery_source_current(const dj_link_discovery_t *s, uint8_t number, uint64_t epoch)
{
    if (!s || s->phase == DJ_LINK_OFF || s->phase == DJ_LINK_WAIT_IP || !epoch) return false;
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i)
        if (s->peers[i].present && s->peers[i].number == number)
            return s->peers[i].source_epoch == epoch;
    return false;
}
