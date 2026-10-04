/* Host tests for the esp-djlink codec, built around packet layouts from the
 * DJ Link Ecosystem Analysis (Deep Symmetry) and validated implementations
 * (python-prodj-link). All multi-byte fields are big-endian. */
#include "djlink.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long _a = (long)(a), _b = (long)(b);                                 \
        if (_a != _b) {                                                      \
            printf("FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__, __LINE__, \
                   #a, #b, _a, _b);                                          \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_magic(void)
{
    uint8_t garbage[16] = {0x51, 0x73, 0x70, 0x74};
    CHECK(djlink_packet_is_valid(garbage, 16) == false);
    CHECK_EQ(djlink_packet_type(garbage, 16), DJLINK_ERR_MAGIC);
    memcpy(garbage, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    garbage[0x0a] = DJLINK_TYPE_BEAT;
    CHECK(djlink_packet_is_valid(garbage, 16));
    CHECK_EQ(djlink_packet_type(garbage, 16), DJLINK_TYPE_BEAT);
}

static void test_pitch_helpers(void)
{
    CHECK(djlink_pitch_raw_to_percent(0x100000) > -0.001f &&
          djlink_pitch_raw_to_percent(0x100000) < 0.001f);
    CHECK(djlink_pitch_raw_to_percent(0x200000) > 99.9f);
    CHECK(djlink_pitch_raw_to_percent(0x000000) < -99.9f);
    CHECK(djlink_pitch_raw_to_percent(0x180000) > 49.9f &&
          djlink_pitch_raw_to_percent(0x180000) < 50.1f);
    CHECK_EQ(djlink_pitch_percent_to_raw(0.0f), 0x100000);
    CHECK_EQ(djlink_pitch_percent_to_raw(100.0f), 0x200000);

    /* 128.0 BPM at 0% = 128.0; at +6.25% = 136.0 */
    float bpm = djlink_effective_bpm(12800, 0x100000);
    CHECK(bpm > 127.99f && bpm < 128.01f);
    bpm = djlink_effective_bpm(12800, 0x110000);
    CHECK(bpm > 135.99f && bpm < 136.01f);
}

static void test_beat_roundtrip(void)
{
    uint8_t pkt[0x60];
    djlink_beat_t b;
    uint8_t out[0x60];
    int n;
    char name[DJLINK_NAME_LEN + 1];

    /* Beat packet template matching the bytefield in beats.html:
     * magic, type 0x28, name at 0x0b, D=1, subtype 0x3c, timings. */
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    pkt[0x0a] = DJLINK_TYPE_BEAT;
    memcpy(&pkt[0x0b], "Pajoniiir", 9);
    pkt[0x1f] = 0x01;
    pkt[0x20] = 0x00;
    pkt[0x21] = 0x01;
    pkt[0x22] = 0x00;
    pkt[0x23] = 0x3c;
    pkt[0x24] = 0x00; pkt[0x25] = 0x00; pkt[0x26] = 0x01; pkt[0x27] = 0x90; /* 400 ms */
    pkt[0x28] = 0x00; pkt[0x29] = 0x00; pkt[0x2a] = 0x03; pkt[0x2b] = 0x20; /* 800 ms */
    pkt[0x2c] = 0x00; pkt[0x2d] = 0x00; pkt[0x2e] = 0x04; pkt[0x2f] = 0xb0; /* 1200 ms */
    pkt[0x30] = 0x00; pkt[0x31] = 0x00; pkt[0x32] = 0x06; pkt[0x33] = 0x40; /* 1600 ms */
    pkt[0x34] = 0x00; pkt[0x35] = 0x00; pkt[0x36] = 0x09; pkt[0x37] = 0x60; /* 2400 ms */
    pkt[0x38] = 0x00; pkt[0x39] = 0x00; pkt[0x3a] = 0x0c; pkt[0x3b] = 0x80; /* 3200 ms */
    memset(&pkt[0x3c], 0xff, 0x54 - 0x3c); /* timings past track end */
    pkt[0x54] = 0x00; pkt[0x55] = 0x10; pkt[0x56] = 0x00; pkt[0x57] = 0x00;
    pkt[0x5a] = 0x2e; pkt[0x5b] = 0xe0; /* 12000 = 120.00 BPM */
    pkt[0x5c] = 0x01;
    pkt[0x5f] = 0x01;

    CHECK_EQ(djlink_beat_parse(pkt, sizeof(pkt), &b), DJLINK_OK);
    CHECK_EQ(b.device_number, 1);
    CHECK_EQ(b.next_beat_ms, 400);
    CHECK_EQ(b.second_beat_ms, 800);
    CHECK_EQ(b.next_bar_ms, 1200);
    CHECK_EQ(b.fourth_beat_ms, 1600);
    CHECK_EQ(b.eighth_beat_ms, 3200);
    CHECK_EQ(b.pitch_raw, 0x100000);
    CHECK_EQ(b.bpm100, 12000);
    CHECK_EQ(b.beat_in_bar, 1);
    djlink_name_to_str(b.name, name);
    CHECK(strcmp(name, "Pajoniiir") == 0);

    /* Bytes 0x3c..0x53 carry no structured data (0xff here = "past track
     * end"); the builder leaves them zero, so clear them in the template
     * before the byte-exact roundtrip check. */
    memset(&pkt[0x3c], 0, 0x54 - 0x3c);

    n = djlink_beat_build(&b, out, sizeof(out));
    CHECK_EQ(n, 0x60);
    CHECK(memcmp(out, pkt, 0x60) == 0);

    /* Truncated packet must fail cleanly. */
    CHECK_EQ(djlink_beat_parse(pkt, 0x30, &b), DJLINK_ERR_TRUNCATED);
    /* Wrong type must fail cleanly. */
    pkt[0x0a] = 0x29;
    CHECK_EQ(djlink_beat_parse(pkt, 0x60, &b), DJLINK_ERR_TYPE);
}

static void test_position_roundtrip(void)
{
    djlink_position_t p;
    uint8_t pkt[0x3c];
    uint8_t out[0x3c];
    int n;

    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    pkt[0x0a] = DJLINK_TYPE_ABS_POSITION;
    memcpy(&pkt[0x0b], "CDJ-3000", 8);
    pkt[0x1f] = 0x01;
    pkt[0x20] = 0x00;
    pkt[0x21] = 2;
    pkt[0x22] = 0x00;
    pkt[0x23] = 0x09;
    pkt[0x27] = 0x2c; /* 44 s */
    pkt[0x28] = 0x12; pkt[0x2b] = 0xa0; /* playhead 0x120000a0 ms */
    pkt[0x2e] = 0x01; pkt[0x2f] = 0x46; /* pitch 326 -> 3.26% */
    pkt[0x3a] = 0x04; pkt[0x3b] = 0xb2; /* 1202 = 120.2 BPM */

    CHECK_EQ(djlink_position_parse(pkt, sizeof(pkt), &p), DJLINK_OK);
    CHECK_EQ(p.device_number, 2);
    CHECK_EQ(p.track_length_s, 44);
    CHECK_EQ(p.playhead_ms, 0x120000a0);
    CHECK_EQ(p.pitch_x100, 326);
    CHECK_EQ(p.bpm10, 1202);

    n = djlink_position_build(&p, out, sizeof(out));
    CHECK_EQ(n, 0x3c);
    CHECK(memcmp(out, pkt, 0x3c) == 0);
}

static void test_status_parse(void)
{
    djlink_status_t s;
    uint8_t pkt[0xd4]; /* nexus length */

    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    pkt[0x0a] = DJLINK_TYPE_CDJ_STATUS;
    memcpy(&pkt[0x0b], "CDJ-2000nexus", 13);
    pkt[0x20] = 0x03; /* revision */
    pkt[0x21] = 1;
    pkt[0x22] = 0x00; pkt[0x23] = 0xb0;
    pkt[0x26] = 0x00; pkt[0x27] = 0x01; /* activity: active */
    pkt[0x29] = DJLINK_SLOT_USB;        /* loaded from USB */
    pkt[0x2c] = 0x00; pkt[0x2d] = 0x01; pkt[0x2e] = 0x00; pkt[0x2f] = 0x2a;
    pkt[0x78] = 0x00; pkt[0x79] = 0x00; pkt[0x7a] = 0x00; pkt[0x7b] = DJLINK_PLAY_PLAYING;
    pkt[0x89] = DJLINK_FLAG_PLAYING | DJLINK_FLAG_MASTER;
    pkt[0x8c] = 0x00; pkt[0x8d] = 0x10; pkt[0x8e] = 0x00; pkt[0x8f] = 0x00;
    pkt[0x90] = 0x80; pkt[0x91] = 0x00;                 /* bpm_state rekordbox */
    pkt[0x92] = 0x2e; pkt[0x93] = 0xe0;                 /* 120.00 BPM */
    pkt[0xa0] = 0x00; pkt[0xa1] = 0x00; pkt[0xa2] = 0x00; pkt[0xa3] = 0x41; /* beat 65 */
    pkt[0xa6] = 2;

    CHECK_EQ(djlink_status_parse(pkt, sizeof(pkt), &s), DJLINK_OK);
    CHECK_EQ(s.revision, 0x03);
    CHECK_EQ(s.device_number, 1);
    CHECK_EQ(s.lenr, 0xb0);
    CHECK(s.active);
    CHECK_EQ(s.source_slot, DJLINK_SLOT_USB);
    CHECK_EQ(s.rekordbox_id, 0x1002a);
    CHECK_EQ(s.play_state, DJLINK_PLAY_PLAYING);
    CHECK(s.has_flag_bits);
    CHECK((s.flags & DJLINK_FLAG_MASTER) && (s.flags & DJLINK_FLAG_PLAYING) &&
          !(s.flags & DJLINK_FLAG_ON_AIR));
    CHECK_EQ(s.pitch_raw, 0x100000);
    CHECK_EQ(s.bpm_state, 0x8000);
    CHECK_EQ(s.bpm100, 12000);
    CHECK_EQ(s.beat, 65);
    CHECK_EQ(s.beat_in_bar, 2);
    CHECK_EQ(s.master_handoff, 0x00);   /* raw byte, not rewritten */
    pkt[0x9f] = 0xff;
    CHECK_EQ(djlink_status_parse(pkt, sizeof(pkt), &s), DJLINK_OK);
    CHECK_EQ(s.master_handoff, 0xff);
    pkt[0x9f] = 4;
    CHECK_EQ(djlink_status_parse(pkt, sizeof(pkt), &s), DJLINK_OK);
    CHECK_EQ(s.master_handoff, 4);
    CHECK_EQ(djlink_status_parse(pkt, 0x9f, &s), DJLINK_OK);
    CHECK_EQ(s.master_handoff, 0xff);   /* not present = none */

    /* Older/shorter players: missing fields zeroed, no crash. */
    CHECK_EQ(djlink_status_parse(pkt, 0x40, &s), DJLINK_OK);
    CHECK(s.active);
    CHECK(!s.has_flag_bits);
    CHECK_EQ(s.bpm100, 0);
}

/* Build -> parse round trip, plus the offsets vynull's parser reads. */
static void test_status_build(void)
{
    djlink_status_t in, s;
    uint8_t pkt[DJLINK_STATUS_PACKET_LEN];
    memset(&in, 0, sizeof(in));
    djlink_name_from_str("PAJONIIIR", in.name);
    in.device_number = 3;
    in.active = true;
    in.source_device = 17;
    in.source_slot = DJLINK_SLOT_LAPTOP;
    in.rekordbox_id = 0x1234;
    in.play_state = DJLINK_PLAY_PLAYING;
    in.flags = DJLINK_FLAG_PLAYING | DJLINK_FLAG_SYNC;
    in.pitch_raw = 0x00100000 + 0x0147ae; /* +8% */
    in.bpm100 = 12800;
    in.beat = 0xffffffffu;
    CHECK_EQ(djlink_status_build(&in, 7, pkt, sizeof(pkt) - 1u), DJLINK_ERR_BOUNDS);
    CHECK_EQ(djlink_status_build(NULL, 7, pkt, sizeof(pkt)), DJLINK_ERR_NULL);
    CHECK_EQ(djlink_status_build(&in, 7, pkt, sizeof(pkt)), (int)DJLINK_STATUS_PACKET_LEN);
    CHECK(djlink_packet_is_valid(pkt, sizeof(pkt)));
    CHECK_EQ(djlink_status_parse(pkt, sizeof(pkt), &s), DJLINK_OK);
    CHECK(memcmp(s.name, in.name, DJLINK_NAME_LEN) == 0);
    CHECK_EQ(s.revision, 0x03);
    CHECK_EQ(s.device_number, 3);
    CHECK_EQ(s.lenr, 0xb0);
    CHECK(s.active);
    CHECK_EQ(s.source_device, 17);
    CHECK_EQ(s.source_slot, DJLINK_SLOT_LAPTOP);
    CHECK_EQ(s.rekordbox_id, 0x1234);
    CHECK_EQ(s.play_state, DJLINK_PLAY_PLAYING);
    CHECK_EQ(s.flags, DJLINK_FLAG_PLAYING | DJLINK_FLAG_SYNC);
    CHECK_EQ(s.pitch_raw, 0x00100000 + 0x0147ae);
    CHECK_EQ(s.bpm_state, 0x8000);
    CHECK_EQ(s.bpm100, 12800);
    CHECK_EQ(s.beat, 0xffffffffu);
    CHECK_EQ(pkt[0x24], 3);              /* vynull: device number */
    CHECK_EQ(pkt[0x27], 1);              /* vynull: active */
    CHECK_EQ(pkt[0x2a], 0x01);           /* rekordbox track */
    CHECK_EQ(pkt[0x8b], 0x7a);           /* P2 playing */
    CHECK_EQ(pkt[0x9d], 0x09);           /* P3 playing */
    CHECK_EQ(djlink_rd32(&pkt[0xc8]), 7u);
    CHECK_EQ(pkt[0x9f], 0xff);           /* Mh: 0 builds as none */
    CHECK_EQ(s.master_handoff, 0xff);
    in.master_handoff = 2;
    CHECK_EQ(djlink_status_build(&in, 7, pkt, sizeof(pkt)), (int)DJLINK_STATUS_PACKET_LEN);
    CHECK_EQ(pkt[0x9f], 2);
    CHECK_EQ(djlink_status_parse(pkt, sizeof(pkt), &s), DJLINK_OK);
    CHECK_EQ(s.master_handoff, 2);
    in.master_handoff = 0;

    /* No track: P3 0, track type 0. */
    in.play_state = DJLINK_PLAY_NO_TRACK;
    in.flags = 0;
    in.bpm100 = 0xffff;
    CHECK_EQ(djlink_status_build(&in, 8, pkt, sizeof(pkt)), (int)DJLINK_STATUS_PACKET_LEN);
    CHECK_EQ(pkt[0x2a], 0x00);
    CHECK_EQ(pkt[0x8b], 0x7e);
    CHECK_EQ(pkt[0x9d], 0x00);
}

static void test_keepalive(void)
{
    djlink_keepalive_t k;
    uint8_t out[0x36];
    int n;
    char name[DJLINK_NAME_LEN + 1];
    uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xaa, 0xbb, 0xcc};

    memset(&k, 0, sizeof(k));
    k.name = "Pajoniiir P4";
    k.device_number = 7;
    memcpy(k.mac, mac, 6);
    k.ip = 0xc0a80066u; /* 192.168.0.102 */
    k.peer_count = 3;
    k.startup_flags = 0x01;

    n = djlink_keepalive_build(&k, out, sizeof(out));
    CHECK_EQ(n, 0x36);
    CHECK(memcmp(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN) == 0);
    CHECK_EQ(out[0x0a], DJLINK_TYPE_KEEPALIVE);
    djlink_name_to_str(&out[0x0c], name);
    CHECK(strcmp(name, "Pajoniiir P4") == 0);
    CHECK_EQ(out[0x20], 0x01);
    CHECK_EQ(out[0x21], 0x02); /* CDJ device type */
    CHECK_EQ(out[0x23], 0x36); /* subtype */
    CHECK_EQ(out[0x24], 7);
    CHECK_EQ(out[0x25], 0x01);
    CHECK(memcmp(&out[0x26], mac, 6) == 0);
    CHECK_EQ(out[0x2c], 0xc0); /* IP big-endian */
    CHECK_EQ(out[0x2f], 0x66);
    CHECK_EQ(out[0x30], 3);
    CHECK_EQ(out[0x34], 0x01);
}

int main(void)
{
    test_magic();
    test_pitch_helpers();
    test_beat_roundtrip();
    test_position_roundtrip();
    test_status_parse();
    test_status_build();
    test_keepalive();
    if (failures == 0) {
        printf("all djlink tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
