#include "dj_link_discovery.h"
#include "djlink/claim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t local_mac[6] = {2,0,0,0,0,1};
static const uint32_t local_ip = 0xc0a80102u;
static void start(dj_link_discovery_t *s, uint32_t now)
{
    dj_link_discovery_init(s);
    dj_link_discovery_configure(s, true, local_ip, local_mac, now);
}
static void ka(dj_link_discovery_t *s, uint8_t number, uint32_t ip, uint8_t suffix, uint32_t now)
{
    uint8_t buf[DJLINK_KEEPALIVE_PACKET_LEN];
    djlink_keepalive_t k = {.name="peer", .device_number=number, .ip=ip};
    k.mac[5] = suffix;
    assert(djlink_keepalive_build(&k, buf, sizeof(buf)) == sizeof(buf));
    assert(dj_link_discovery_ingest(s, 50000, buf, sizeof(buf), ip, now));
}
static void complete(dj_link_discovery_t *s, uint32_t begin)
{
    uint8_t buf[DJLINK_MAX_PACKET]; unsigned d;
    for (uint32_t step = 0; step < 13; ++step) {
        uint32_t now = begin + step * DJ_LINK_CLAIM_MS;
        for (unsigned i = 0; i < 2; ++i) {
            int n = dj_link_discovery_poll(s, now, buf, sizeof(buf), &d);
            if (n) {
                assert(d == i);
                assert(djlink_packet_is_valid(buf, (size_t)n));
                if (buf[0x0a] == 2) assert(buf[0x2e] >= 1 && buf[0x2e] <= 6);
            }
        }
    }
}
static void all_occupancy_combinations(void)
{
    for (unsigned occupied = 0; occupied < 64; ++occupied) {
        dj_link_discovery_t s; start(&s, 0); unsigned free_count = 6;
        for (uint8_t n = 1; n <= 6; ++n)
            if (occupied & (1u << (n-1))) { ka(&s, n, 0xc0a80110u+n, n, 0); --free_count; }
        complete(&s, 0);
        uint8_t a = dj_link_discovery_number(&s, 0), b = dj_link_discovery_number(&s, 1);
        if (free_count >= 2) {
            assert(s.phase == DJ_LINK_ACTIVE && a && b && a != b);
            assert(!(occupied & (1u << (a-1))) && !(occupied & (1u << (b-1))));
        } else {
            assert(s.phase == DJ_LINK_OBSERVER && s.reason == DJ_LINK_REASON_NO_PAIR);
            assert(!a && !b && !s.numbers[0] && !s.numbers[1]);
        }
    }
}
static void conflict_and_expiry(void)
{
    dj_link_discovery_t s; start(&s, 0); complete(&s, 0);
    assert(s.numbers[0] == 3 && s.numbers[1] == 4);
    uint64_t previous = s.epoch;
    ka(&s, 3, 0xc0a80103, 3, 3800);
    assert(s.phase == DJ_LINK_OBSERVER && s.conflicts == 1 && s.epoch > previous);
    assert(!dj_link_discovery_number(&s, 1));
    uint64_t peer_epoch = s.peers[0].source_epoch;
    assert(dj_link_discovery_source_current(&s, 3, peer_epoch));
    complete(&s, 3800);
    assert(s.phase == DJ_LINK_ACTIVE && s.numbers[0] != 3 && s.numbers[1] != 3);
    ka(&s, 3, 0xc0a80103, 33, 7800); /* same player/IP, replaced physical peer */
    assert(!dj_link_discovery_source_current(&s, 3, peer_epoch));
    peer_epoch = s.peers[0].source_epoch;
    ka(&s, 3, 0xc0a80199, 33, 8000); /* IP changed, stale work must fail */
    assert(!dj_link_discovery_source_current(&s, 3, peer_epoch));
    peer_epoch = s.peers[0].source_epoch;
    dj_link_discovery_tick(&s, 13000);
    assert(!dj_link_discovery_source_current(&s, 3, peer_epoch) && s.peer_expirations == 1);
    ka(&s, 3, 0xc0a80199, 33, 13001);
    assert(!dj_link_discovery_source_current(&s, 3, peer_epoch));
    previous = s.epoch;
    dj_link_discovery_configure(&s, true, 0, local_mac, 14000);
    assert(s.phase == DJ_LINK_WAIT_IP && s.epoch > previous && !s.peers[0].present);
    dj_link_discovery_configure(&s, true, local_ip, local_mac, 15000);
    assert(s.phase == DJ_LINK_ANNOUNCE);
}
static void bounds_status_and_wrap(void)
{
    dj_link_discovery_t s; start(&s, UINT32_MAX-1000);
    uint8_t buf[DJLINK_MAX_PACKET]; unsigned d = 77;
    assert(!dj_link_discovery_poll(&s, UINT32_MAX-1000, buf, 2, &d));
    assert(s.sent_mask == 0 && s.iteration == 1 && d == 77);
    complete(&s, UINT32_MAX-1000);
    assert(s.phase == DJ_LINK_ACTIVE);
    djlink_status_t status = {.device_number=1, .flags=DJLINK_FLAG_MASTER|DJLINK_FLAG_PLAYING,
        .bpm100=12800, .pitch_raw=0x100000};
    djlink_name_from_str("CDJ", status.name);
    int len = djlink_status_build(&status, 1, buf, sizeof(buf)); assert(len > 0);
    assert(dj_link_discovery_ingest(&s, 50002, buf, (size_t)len, 0xc0a80110, 3100));
    assert(s.peers[0].has_status && s.peers[0].status.flags == status.flags);
    uint64_t epoch = s.peers[0].source_epoch;
    djlink_beat_t beat = {.device_number=1, .beat_in_bar=4, .bpm100=12800, .pitch_raw=0x100000};
    len = djlink_beat_build(&beat, buf, sizeof(buf)); assert(len > 0);
    for (int cut = 0; cut < len; ++cut)
        assert(!dj_link_discovery_ingest(&s, 50001, buf, (size_t)cut, 0xc0a80110, 3200));
    assert(dj_link_discovery_ingest(&s, 50001, buf, (size_t)len, 0xc0a80110, 3200));
    assert(s.peers[0].source_epoch == epoch && s.peers[0].has_beat);
    assert(!dj_link_discovery_ingest(&s, 50000, buf, (size_t)len, 0xc0a80110, 3200));
    assert(!dj_link_discovery_ingest(&s, 50001, buf, (size_t)len, local_ip, 3200));
    assert(!dj_link_discovery_ingest(&s, 50001, NULL, 100, 0xc0a80110, 3200));
    buf[0x5c] = 7;
    assert(!dj_link_discovery_ingest(&s, 50001, buf, (size_t)len, 0xc0a80110, 3200));
    dj_link_discovery_configure(&s, false, local_ip, local_mac, 3300);
    assert(s.phase == DJ_LINK_OFF && !dj_link_discovery_source_current(&s, 1, epoch));
}
static void late_claim_and_full_table(void)
{
    dj_link_discovery_t s; start(&s, 0);
    uint8_t buf[DJLINK_MAX_PACKET]; unsigned d;
    for (uint32_t now = 0; now <= 1500; now += 300) {
        assert(dj_link_discovery_poll(&s, now, buf, sizeof(buf), &d));
        assert(dj_link_discovery_poll(&s, now, buf, sizeof(buf), &d));
    }
    assert(s.phase == DJ_LINK_CLAIM_IP);
    djlink_claim_ip_t c = {.name="newcomer", .device_number=s.numbers[1], .iteration=1};
    c.mac[5] = 99;
    int n = djlink_claim_ip_build(&c, buf, sizeof(buf)); assert(n > 0);
    assert(!dj_link_discovery_ingest(&s, 50000, buf, (size_t)n-1, 0xc0a80120, 1700));
    assert(s.phase == DJ_LINK_CLAIM_IP);
    assert(dj_link_discovery_ingest(&s, 50000, buf, (size_t)n, 0xc0a80120, 1700));
    assert(s.phase == DJ_LINK_OBSERVER && !s.numbers[0] && !s.numbers[1]);
    for (uint8_t i = 17; i < 27; ++i) {
        djlink_keepalive_t k = {.name="rekordbox", .device_number=i}; k.mac[5] = i;
        n = djlink_keepalive_build(&k, buf, sizeof(buf)); assert(n > 0); buf[0x21] = 3;
        assert(dj_link_discovery_ingest(&s, 50000, buf, (size_t)n, 0xc0a80120u+i, 1800+i));
    }
    unsigned count = 0;
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i) count += s.peers[i].present;
    assert(count == DJ_LINK_PEERS);
    assert(!dj_link_discovery_source_current(&s, 17, s.epoch));
}
static void observer_recovers_and_rekordbox(void)
{
    dj_link_discovery_t s; start(&s, 0);
    for (uint8_t i = 1; i <= 6; ++i) ka(&s, i, 0xc0a80110u+i, i, 0);
    complete(&s, 0); assert(s.phase == DJ_LINK_OBSERVER);
    for (uint8_t i = 1; i <= 4; ++i) ka(&s, i, 0xc0a80110u+i, i, 4900);
    complete(&s, 5900);
    assert(s.phase == DJ_LINK_ACTIVE && s.numbers[0] == 5 && s.numbers[1] == 6);
    uint8_t buf[DJLINK_MAX_PACKET]; unsigned deck;
    assert(dj_link_discovery_poll(&s, s.next_ms, buf, sizeof(buf), &deck) > 0);
    assert(buf[0x0a] == 6 && buf[0x35] == 0x64);
    djlink_keepalive_t k = {.name="rekordbox", .device_number=17}; k.mac[5] = 77;
    int n = djlink_keepalive_build(&k, buf, sizeof(buf)); buf[0x21] = 3;
    assert(dj_link_discovery_ingest(&s, 50000, buf, (size_t)n, 0xc0a80177, 9000));
    bool found = false;
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i)
        if (s.peers[i].present && s.peers[i].number == 17) {
            assert(s.peers[i].device_type == 3); found = true;
        }
    assert(found);
}
int main(void)
{
    all_occupancy_combinations(); conflict_and_expiry();
    bounds_status_and_wrap(); late_claim_and_full_table();
    observer_recovers_and_rekordbox();
    puts("PASS dual-player claim, all 64 occupancy sets, conflict, epochs, bounds, wrap");
    return 0;
}
