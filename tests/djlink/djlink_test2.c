/* Host tests: claims, sync/handoff, media, mixer/load, dbserver. */
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

static void test_claims(void)
{
    uint8_t out[0x40];
    int n;
    djlink_claim_info_t info;

    djlink_announce_t an = {0};
    an.name = "Pajoniiir";
    an.device_type = DJLINK_DEVICE_TYPE_CDJ;
    an.payload_byte = 0x01;
    n = djlink_announce_build(&an, out, sizeof(out));
    CHECK_EQ(n, 0x25);
    CHECK_EQ(out[0x0a], 0x0a);
    CHECK_EQ(out[0x21], DJLINK_DEVICE_TYPE_CDJ);
    CHECK_EQ(out[0x24], 0x01);
    CHECK_EQ(djlink_claim_info_parse(out, (size_t)n, &info), DJLINK_OK);
    CHECK_EQ(info.device_type, DJLINK_DEVICE_TYPE_CDJ);
    CHECK_EQ(info.device_number, 0);

    djlink_claim_mac_t cm = {0};
    cm.name = "Pajoniiir";
    cm.device_type = DJLINK_DEVICE_TYPE_CDJ;
    cm.iteration = 2;
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    memcpy(cm.mac, mac, 6);
    n = djlink_claim_mac_build(&cm, out, sizeof(out));
    CHECK_EQ(n, 0x2c);
    CHECK_EQ(out[0x0a], 0x00);
    CHECK_EQ(out[0x24], 2);
    CHECK_EQ(out[0x25], 0x01);
    CHECK(memcmp(&out[0x26], mac, 6) == 0);
    CHECK_EQ(djlink_claim_info_parse(out, (size_t)n, &info), DJLINK_OK);
    CHECK_EQ(info.iteration, 2);

    djlink_claim_ip_t ci = {0};
    ci.name = "Pajoniiir";
    ci.device_type = DJLINK_DEVICE_TYPE_CDJ;
    ci.ip = 0xc0a80066u;
    memcpy(ci.mac, mac, 6);
    ci.device_number = 5;
    ci.iteration = 3;
    ci.auto_assign = 0x02;
    n = djlink_claim_ip_build(&ci, out, sizeof(out));
    CHECK_EQ(n, 0x32);
    CHECK_EQ(out[0x0a], 0x02);
    CHECK_EQ(out[0x24], 0xc0);
    CHECK_EQ(out[0x2e], 5); /* doc: D at byte 0x2e */
    CHECK_EQ(out[0x2f], 3);
    CHECK_EQ(out[0x31], 0x02);
    CHECK_EQ(djlink_claim_info_parse(out, (size_t)n, &info), DJLINK_OK);
    CHECK_EQ(info.device_number, 5);
    CHECK_EQ(info.iteration, 3);

    djlink_claim_final_t cf = {0};
    cf.name = "Pajoniiir";
    cf.device_type = DJLINK_DEVICE_TYPE_CDJ;
    cf.device_number = 5;
    cf.iteration = 1;
    n = djlink_claim_final_build(&cf, out, sizeof(out));
    CHECK_EQ(n, 0x2a); /* doc-reported length */
    CHECK_EQ(out[0x0a], 0x04);
    CHECK_EQ(out[0x24], 5);
    CHECK_EQ(out[0x25], 1);

    djlink_assign_finished_t af = {0};
    af.name = "Pajoniiir";
    af.device_number = 2;
    n = djlink_assign_finished_build(&af, out, sizeof(out));
    CHECK_EQ(n, 0x25);
    CHECK_EQ(out[0x0a], 0x05);
    CHECK_EQ(out[0x24], 2);

    /* Channel conflict parse. */
    memset(out, 0, 0x25);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = 0x08;
    out[0x24] = 3;
    djlink_conflict_t conflict;
    CHECK_EQ(djlink_conflict_parse(out, 0x25, &conflict), DJLINK_OK);
    CHECK_EQ(conflict.device_number, 3);
    CHECK_EQ(djlink_conflict_parse(out, 0x20, &conflict), DJLINK_ERR_TRUNCATED);
}

static void test_sync(void)
{
    uint8_t out[0x40];
    int n;

    djlink_sync_t sc = {0};
    memcpy(sc.name, "Pajoniiir", 9);
    sc.sender_number = 4;
    sc.target_number = 2;
    sc.action = DJLINK_SYNC_ON;
    n = djlink_sync_build(&sc, out, sizeof(out));
    CHECK_EQ(n, 0x2c);
    CHECK_EQ(out[0x0a], 0x2a);
    CHECK_EQ(out[0x21], 4);
    CHECK_EQ(out[0x23], 0x08);
    CHECK_EQ(out[0x27], 2);
    CHECK_EQ(out[0x2b], 0x10);
    djlink_sync_t back;
    CHECK_EQ(djlink_sync_parse(out, (size_t)n, &back), DJLINK_OK);
    CHECK_EQ(back.sender_number, 4);
    CHECK_EQ(back.target_number, 2);
    CHECK_EQ(back.action, DJLINK_SYNC_ON);
    CHECK_EQ(djlink_sync_parse(out, 0x20, &back), DJLINK_ERR_TRUNCATED);

    djlink_handoff_req_t hr = {0};
    hr.name = "Pajoniiir";
    hr.requester_number = 4;
    n = djlink_handoff_req_build(&hr, out, sizeof(out));
    CHECK_EQ(n, 0x28);
    CHECK_EQ(out[0x0a], 0x26);
    CHECK_EQ(out[0x27], 4);
    djlink_handoff_req_t hr2;
    CHECK_EQ(djlink_handoff_req_parse(out, (size_t)n, &hr2), DJLINK_OK);
    CHECK_EQ(hr2.requester_number, 4);

    djlink_handoff_resp_t hs = {0};
    hs.name = "CDJ-2000nexus";
    hs.requester_number = 4;
    n = djlink_handoff_resp_build(&hs, out, sizeof(out));
    CHECK_EQ(n, 0x2c);
    CHECK_EQ(out[0x0a], 0x27);
    CHECK_EQ(out[0x27], 4);
    CHECK_EQ(out[0x2b], 0x01);
    djlink_handoff_resp_t hs2;
    CHECK_EQ(djlink_handoff_resp_parse(out, (size_t)n, &hs2), DJLINK_OK);
    CHECK_EQ(hs2.requester_number, 4);
}

static void test_mixer_packets(void)
{
    uint8_t out[0x60];
    int n;

    /* Mixer status parse (template from the analysis: F=0xf0 master). */
    uint8_t pkt[0x38];
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    pkt[0x0a] = 0x29;
    memcpy(&pkt[0x0b], "DJM-2000nexus", 13);
    pkt[0x20] = 0x01;
    pkt[0x21] = 0x21;
    pkt[0x22] = 0x00; pkt[0x23] = 0x14;
    pkt[0x27] = 0xf0; /* flags: master */
    pkt[0x28] = 0x00; pkt[0x29] = 0x10; pkt[0x2a] = 0x00; pkt[0x2b] = 0x00;
    pkt[0x2c] = 0x80; pkt[0x2d] = 0x00; /* bpm_state */
    pkt[0x2e] = 0x2e; pkt[0x2f] = 0xe0; /* 120.00 BPM */
    pkt[0x36] = 0x02; /* handing off to player 2 */
    pkt[0x37] = 3;
    djlink_mixer_status_t ms;
    CHECK_EQ(djlink_mixer_status_parse(pkt, sizeof(pkt), &ms), DJLINK_OK);
    CHECK_EQ(ms.device_number, 0x21);
    CHECK((ms.flags & DJLINK_FLAG_MASTER));
    CHECK_EQ(ms.pitch_raw, 0x100000);
    CHECK_EQ(ms.bpm100, 12000);
    CHECK_EQ(ms.master_handoff, 2);
    CHECK_EQ(ms.beat_in_bar, 3);

    /* Fader start. */
    djlink_fader_t f = {0};
    memcpy(f.name, "DJM-2000nexus", 13);
    f.player_number = 0x21;
    f.players[0] = DJLINK_FADER_START;
    f.players[1] = DJLINK_FADER_STOP;
    f.players[2] = DJLINK_FADER_IGNORE;
    f.players[3] = DJLINK_FADER_IGNORE;
    n = djlink_fader_build(&f, out, sizeof(out));
    CHECK_EQ(n, 0x28);
    CHECK_EQ(out[0x0a], 0x02);
    CHECK_EQ(out[0x24], 0x00);
    CHECK_EQ(out[0x25], 0x01);
    djlink_fader_t f2;
    CHECK_EQ(djlink_fader_parse(out, (size_t)n, &f2), DJLINK_OK);
    CHECK_EQ(f2.players[0], DJLINK_FADER_START);
    CHECK_EQ(f2.players[1], DJLINK_FADER_STOP);

    /* Channels on-air. */
    djlink_onair_t oa = {0};
    memcpy(oa.name, "DJM-2000nexus", 13);
    oa.mixer_number = 0x21;
    oa.on_air[0] = 1;
    oa.on_air[1] = 0;
    oa.on_air[2] = 0;
    oa.on_air[3] = 1;
    n = djlink_onair_build(&oa, out, sizeof(out));
    CHECK_EQ(n, 0x28);
    CHECK_EQ(out[0x0a], 0x03);
    djlink_onair_t oa2;
    CHECK_EQ(djlink_onair_parse(out, (size_t)n, &oa2), DJLINK_OK);
    CHECK(oa2.on_air[0] && oa2.on_air[3] && !oa2.on_air[1]);

    /* Load track + ack. */
    djlink_load_track_t lt = {0};
    lt.name = "Pajoniiir";
    lt.sender_number = 4;
    lt.target_player = 1;
    lt.slot = DJLINK_SLOT_USB;
    lt.rekordbox_id = 0x0000D18;
    n = djlink_load_track_build(&lt, out, sizeof(out));
    CHECK_EQ(n, 0x58);
    CHECK_EQ(out[0x0a], 0x19);
    CHECK_EQ(out[0x28], 1);
    CHECK_EQ(out[0x29], DJLINK_SLOT_USB);
    CHECK_EQ(out[0x2f], 0x18);
    djlink_load_track_t lt2;
    CHECK_EQ(djlink_load_track_parse(out, (size_t)n, &lt2), DJLINK_OK);
    CHECK_EQ(lt2.rekordbox_id, 0x0000D18);
    CHECK_EQ(lt2.target_player, 1);

    n = djlink_load_ack_build(1, "Pajoniiir", out, sizeof(out));
    CHECK_EQ(n, 0x26);
    CHECK_EQ(out[0x0a], 0x1a);
    djlink_load_ack_t ack;
    CHECK_EQ(djlink_load_ack_parse(out, (size_t)n, &ack), DJLINK_OK);
    CHECK_EQ(ack.device_number, 1);
}

static void test_media(void)
{
    uint8_t out[0xd0];
    int n;

    djlink_media_query_t q = {0};
    q.name = "Pajoniiir";
    q.sender_number = 4;
    q.reply_ip = 0xc0a80066u;
    q.source_device = 1;
    q.slot = DJLINK_SLOT_USB;
    n = djlink_media_query_build(&q, out, sizeof(out));
    CHECK_EQ(n, 0x30);
    CHECK_EQ(out[0x0a], 0x05);
    CHECK_EQ(out[0x24], 0xc0);
    CHECK_EQ(out[0x2b], 1);
    CHECK_EQ(out[0x2f], DJLINK_SLOT_USB);

    /* Response template per media.html. */
    uint8_t pkt[0xc0];
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    pkt[0x0a] = 0x06;
    memcpy(&pkt[0x0b], "CDJ-2000nexus", 13);
    pkt[0x1f] = 0x01;
    pkt[0x21] = 1;
    pkt[0x22] = 0x00; pkt[0x23] = 0x9c;
    pkt[0x27] = 1; /* Dr */
    pkt[0x2b] = 3; /* Sr: USB */
    /* name "USB2" UTF-16BE at 0x2c */
    pkt[0x2c] = 0x00; pkt[0x2d] = 'U';
    pkt[0x2e] = 0x00; pkt[0x2f] = 'S';
    pkt[0x30] = 0x00; pkt[0x31] = 'B';
    pkt[0x32] = 0x00; pkt[0x33] = '2';
    pkt[0xa6] = 0x02; pkt[0xa7] = 0x8b; /* 651 tracks */
    pkt[0xa8] = DJLINK_MEDIA_COLOR_AQUA;
    pkt[0xaa] = 0x01; /* rekordbox */
    pkt[0xae] = 0x00; pkt[0xaf] = 0x0a; /* 10 playlists */
    pkt[0xb3] = 0x08; /* total bytes hi word */
    pkt[0xbb] = 0x01; /* free bytes hi word */
    djlink_media_resp_t r;
    CHECK_EQ(djlink_media_resp_parse(pkt, sizeof(pkt), &r), DJLINK_OK);
    CHECK_EQ(r.slot, DJLINK_SLOT_USB);
    CHECK(strcmp(r.media_name, "USB2") == 0);
    CHECK_EQ(r.track_count, 651);
    CHECK_EQ(r.color, DJLINK_MEDIA_COLOR_AQUA);
    CHECK_EQ(r.track_type, 1);
    CHECK_EQ(r.playlist_count, 10);
    CHECK_EQ(r.total_bytes >> 32, 8);
    CHECK_EQ(r.free_bytes >> 32, 1);

    /* Roundtrip through the builder. */
    n = djlink_media_resp_build(&r, out, sizeof(out));
    CHECK_EQ(n, 0xc0);
    djlink_media_resp_t r2;
    CHECK_EQ(djlink_media_resp_parse(out, (size_t)n, &r2), DJLINK_OK);
    CHECK_EQ(r2.track_count, 651);
    CHECK(strcmp(r2.media_name, "USB2") == 0);

    /* UTF-16 helpers: encode "AZ" then decode back. */
    char decoded[21];
    uint8_t enc[10];
    CHECK_EQ(djlink_str_to_utf16be("AZ", enc, sizeof(enc)), 4);
    CHECK_EQ(enc[0], 0x00);
    CHECK_EQ(enc[1], 'A');
    CHECK_EQ(enc[3], 'Z');
    djlink_utf16be_to_str(enc, 4, decoded, sizeof(decoded));
    CHECK(strcmp(decoded, "AZ") == 0);
}

static void test_dbserver(void)
{
    uint8_t out[512];
    int n;
    uint16_t port;

    n = djlink_db_port_query_build(out, sizeof(out));
    CHECK_EQ(n, 19);
    CHECK_EQ(out[0], 0x00); CHECK_EQ(out[1], 0x00); CHECK_EQ(out[2], 0x00); CHECK_EQ(out[3], 0x0f);
    CHECK(memcmp(&out[4], "RemoteDBServer", 14) == 0);
    CHECK_EQ(out[18], 0x00);
    uint8_t reply[2] = {0x04, 0x1b}; /* 1051 */
    CHECK_EQ(djlink_db_port_query_reply_parse(reply, 2, &port), DJLINK_OK);
    CHECK_EQ(port, 1051);

    n = djlink_db_setup_build(out, sizeof(out));
    CHECK_EQ(n, 5);
    CHECK_EQ(out[0], DJLINK_DB_FIELD_INT32);
    CHECK_EQ(out[4], 0x01);

    /* Context setup: matches the documented byte sequence. */
    n = djlink_db_context_setup_build(2, out, sizeof(out));
    CHECK(n > 0);
    {
        /* Field layout: [0x11][87 23 49 ae][0x11][ff ff ff fe][0x10][type]
         * [0x0f][argc][0x14][00 00 00 0c][tags x12][0x11][arg] */
        CHECK_EQ(out[0], 0x11);
        CHECK_EQ(out[1], 0x87);
        CHECK_EQ(out[4], 0xae);
        CHECK_EQ(out[5], 0x11);        /* txid field */
        CHECK_EQ(out[6], 0xff);
        CHECK_EQ(out[9], 0xfe);        /* txid 0xfffffffe LSB */
        CHECK_EQ(out[10], 0x10);       /* type field */
        CHECK_EQ(out[11], 0x00);
        CHECK_EQ(out[12], 0x00);       /* type 0x0000 */
        CHECK_EQ(out[13], 0x0f);       /* argc field */
        CHECK_EQ(out[14], 1);
        CHECK_EQ(out[15], 0x14);       /* tags blob */
        CHECK_EQ(out[19], 0x0c);       /* blob length 12 */
        CHECK_EQ(out[20], 0x06);       /* int32 arg tag */
        CHECK_EQ(out[31], 0x00);       /* tag padding */
        CHECK_EQ(out[32], 0x11);       /* arg: int32 D */
        CHECK_EQ(out[36], 2);
    }
    djlink_db_msg_t msg;
    CHECK_EQ(djlink_db_msg_parse(out, (size_t)n, &msg), DJLINK_OK);
    CHECK_EQ(msg.txid, 0xfffffffeu);
    CHECK_EQ(msg.type, 0);
    CHECK_EQ(msg.arg_count, 1);
    CHECK_EQ(msg.args[0].num, 2);

    /* Metadata request roundtrip. */
    n = djlink_db_metadata_request_build(1, 1, DJLINK_SLOT_SD, 0x1234, out, sizeof(out));
    CHECK(n > 0);
    CHECK_EQ(djlink_db_msg_parse(out, (size_t)n, &msg), DJLINK_OK);
    CHECK_EQ(msg.type, DJLINK_DB_TYPE_METADATA_REQUEST);
    CHECK_EQ(msg.arg_count, 2);
    CHECK_EQ(msg.args[0].num, 0x01010201u); /* D=1, menu=1, slot=2, Tr=1 */
    CHECK_EQ(msg.args[1].num, 0x1234);

    /* Render request roundtrip. */
    n = djlink_db_render_request_build(2, 1, DJLINK_SLOT_SD, 0, 11, out, sizeof(out));
    CHECK(n > 0);
    CHECK_EQ(djlink_db_msg_parse(out, (size_t)n, &msg), DJLINK_OK);
    CHECK_EQ(msg.type, DJLINK_DB_TYPE_RENDER);
    CHECK_EQ(msg.arg_count, 6);
    CHECK_EQ(msg.args[1].num, 0);
    CHECK_EQ(msg.args[2].num, 11);
    CHECK_EQ(msg.args[4].num, 11);

    /* Parse a success reply with 2 int args (type 4000). */
    {
        djlink_db_arg_t args[2];
        args[0].type = DJLINK_DB_FIELD_INT32;
        args[0].num = 0x2002;
        args[1].type = DJLINK_DB_FIELD_INT32;
        args[1].num = 11;
        n = djlink_db_msg_build(1, DJLINK_DB_TYPE_SUCCESS, args, 2, out, sizeof(out));
        CHECK(n > 0);
        CHECK_EQ(djlink_db_msg_parse(out, (size_t)n, &msg), DJLINK_OK);
        CHECK_EQ(msg.type, DJLINK_DB_TYPE_SUCCESS);
        CHECK_EQ(msg.args[0].num, 0x2002);
        CHECK_EQ(msg.args[1].num, 11);
    }

    /* String + binary args roundtrip. */
    {
        static const uint8_t blob[4] = {1, 2, 3, 4};
        djlink_db_arg_t args[2];
        uint8_t utf16[12];
        memset(utf16, 0, sizeof(utf16));
        utf16[1] = 'a';
        utf16[3] = 'b';
        args[0].type = DJLINK_DB_FIELD_STRING;
        args[0].bin = utf16;
        args[0].bin_len = 6; /* "ab\0" in UTF-16BE */
        args[1].type = DJLINK_DB_FIELD_BINARY;
        args[1].bin = blob;
        args[1].bin_len = 4;
        n = djlink_db_msg_build(7, DJLINK_DB_TYPE_MENU_ITEM, args, 2, out, sizeof(out));
        CHECK(n > 0);
        CHECK_EQ(djlink_db_msg_parse(out, (size_t)n, &msg), DJLINK_OK);
        CHECK_EQ(msg.arg_count, 2);
        CHECK_EQ(msg.args[0].type, DJLINK_DB_FIELD_STRING);
        CHECK_EQ(msg.args[0].bin_len, 6);
        CHECK_EQ(msg.args[1].bin_len, 4);
        CHECK(memcmp(msg.args[1].bin, blob, 4) == 0);
        /* On the wire a string's length counts UTF-16 code units. */
        CHECK(memcmp(&out[n - 9 - 11], "\x26\x00\x00\x00\x03", 5) == 0);
        args[0].bin_len = 5; /* half a code unit */
        CHECK(djlink_db_msg_build(7, DJLINK_DB_TYPE_MENU_ITEM, args, 2, out, sizeof(out)) < 0);
    }

    /* A vynull menu item, as captured: label lengths in code units. */
    {
        static const uint8_t item[] = {
            0x11, 0x87, 0x23, 0x49, 0xae, 0x11, 0x00, 0x00, 0x00, 0x03, 0x10, 0x41,
            0x01, 0x0f, 0x0c, 0x14, 0x00, 0x00, 0x00, 0x0c, 0x06, 0x06, 0x06, 0x02,
            0x06, 0x02, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x11, 0x00, 0x00, 0x32,
            0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x11, 0x00, 0x00, 0x00, 0x26, 0x26,
            0x00, 0x00, 0x00, 0x13, 0x00, 0x42, 0x00, 0x61, 0x00, 0x63, 0x00, 0x6b,
            0x00, 0x20, 0x00, 0x54, 0x00, 0x6f, 0x00, 0x20, 0x00, 0x42, 0x00, 0x61,
            0x00, 0x63, 0x00, 0x6b, 0x00, 0x20, 0x00, 0x28, 0x00, 0x42, 0x00, 0x32,
            0x00, 0x42, 0x00, 0x29, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x14, 0x26,
            0x00, 0x00, 0x00, 0x0a, 0x00, 0x31, 0x00, 0x32, 0x00, 0x38, 0x00, 0x2e,
            0x00, 0x30, 0x00, 0x20, 0x00, 0x62, 0x00, 0x70, 0x00, 0x6d, 0x00, 0x00,
            0x11, 0x00, 0x00, 0x0d, 0x04, 0x11, 0x01, 0x00, 0x00, 0x00, 0x11, 0x00,
            0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x02, 0x11, 0x00, 0x00, 0x01,
            0x00, 0x11, 0x00, 0x00, 0x00, 0x00,
        };
        CHECK_EQ(djlink_db_msg_parse(item, sizeof(item), &msg), DJLINK_OK);
        CHECK_EQ(msg.type, DJLINK_DB_TYPE_MENU_ITEM);
        CHECK_EQ(msg.arg_count, 12);
        CHECK_EQ(msg.args[1].num, 1);
        CHECK_EQ(msg.args[2].num, 0x26);       /* label 1 byte size */
        CHECK_EQ(msg.args[3].bin_len, 0x26);   /* 19 code units */
        CHECK(msg.args[3].bin[1] == 'B' && msg.args[3].bin[35] == ')');
        CHECK_EQ(msg.args[5].bin_len, 0x14);
        CHECK_EQ(msg.args[6].num, 0x0d04);
        CHECK_EQ(msg.args[11].num, 0);
        CHECK(djlink_db_msg_parse(item, sizeof(item) - 1, &msg) == DJLINK_ERR_TRUNCATED);
    }

    /* Truncation safety. */
    CHECK_EQ(djlink_db_msg_parse(out, 4, &msg), DJLINK_ERR_TRUNCATED);

    /* vynull's SETUP reply: the tag blob holds one byte per argument
     * (2 here), not the 12 a player sends. */
    {
        static const uint8_t vynull_setup[] = {
            0x11, 0x87, 0x23, 0x49, 0xae, 0x11, 0xff, 0xff, 0xff, 0xfe,
            0x10, 0x40, 0x00, 0x0f, 0x02, 0x14, 0x00, 0x00, 0x00, 0x02,
            0x06, 0x06, 0x11, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
            0x00, 0x11,
        };
        CHECK_EQ(djlink_db_msg_parse(vynull_setup, sizeof(vynull_setup), &msg), DJLINK_OK);
        CHECK_EQ(msg.type, DJLINK_DB_TYPE_SUCCESS);
        CHECK_EQ(msg.arg_count, 2);
        CHECK_EQ(msg.args[1].num, 17);
        /* A tag blob shorter than the argument count is still refused. */
        CHECK_EQ(djlink_db_msg_parse(vynull_setup, sizeof(vynull_setup) - 5, &msg),
                 DJLINK_ERR_TRUNCATED);
        uint8_t short_tags[sizeof(vynull_setup)];
        memcpy(short_tags, vynull_setup, sizeof(short_tags));
        short_tags[19] = 0x01;
        CHECK(djlink_db_msg_parse(short_tags, sizeof(short_tags), &msg) != DJLINK_OK);
    }
}

int main(void)
{
    test_claims();
    test_sync();
    test_mixer_packets();
    test_media();
    test_dbserver();
    if (failures == 0) {
        printf("all djlink tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
