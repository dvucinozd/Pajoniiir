#include "djlink/status.h"

#include <string.h>

/* CDJ keep-alive (type 0x06, 0x36 bytes), layout per python-prodj-link
 * (KeepAlivePacket, validated against captures) and the DJ Link analysis:
 *   0x00 magic, 0x0a type, 0x0b padding (0x00),
 *   0x0c..0x1f device name (20 bytes),
 *   0x20 0x01, 0x21 device type (1 = mixer, 2 = CDJ),
 *   0x22 padding, 0x23 subtype (0x36 for the status keep-alive),
 *   0x24 device number D,
 *   0x25 startup flag (0x02 = was first device on network, 0x01 = joined),
 *   0x26..0x2b MAC, 0x2c..0x2f IP (BE),
 *   0x30 device/peer count, 0x31..0x33 padding,
 *   0x34 flags (0x01), 0x35 (0x00, 0x64 on CDJ-3000).
 */
int djlink_keepalive_build(const djlink_keepalive_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_KEEPALIVE_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_KEEPALIVE_PACKET_LEN);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = DJLINK_TYPE_KEEPALIVE;
    out[0x0b] = 0x00;
    djlink_name_from_str(in->name, &out[0x0c]);
    out[0x20] = 0x01;
    out[0x21] = 0x02; /* device type: CDJ */
    out[0x22] = 0x00;
    out[0x23] = 0x36; /* subtype */
    out[0x24] = in->device_number;
    out[0x25] = in->startup_flags ? in->startup_flags : 0x01;
    memcpy(&out[0x26], in->mac, 6);
    out[0x2c] = (uint8_t)(in->ip >> 24);
    out[0x2d] = (uint8_t)(in->ip >> 16);
    out[0x2e] = (uint8_t)(in->ip >> 8);
    out[0x2f] = (uint8_t)in->ip;
    out[0x30] = in->peer_count ? in->peer_count : 2;
    out[0x34] = 0x01;
    return (int)DJLINK_KEEPALIVE_PACKET_LEN;
}

/* CDJ status packet (type 0x0a). Header:
 *   0x00 magic, 0x0a type, 0x0b..0x1e device name (20 bytes, no padding),
 *   0x1f 0x01, 0x20 revision (3 nxs, 4 xdj1000, 1 djm/rekordbox),
 *   0x21 device number, 0x22..0x23 remaining length lenr (BE),
 *   0x24 device number (copy), 0x25 flag byte,
 *   content from 0x26: activity u16 BE (0x26), source device 0x28,
 *   source slot 0x29, analyze type 0x2a, rekordbox ID 0x2c, play state u32
 *   0x78..0x7b, flag bits 0x89, physical pitch 0x8c, bpm state 0x90,
 *   BPM x100 0x92, master-meaningful 0x9e, beat counter 0xa0,
 *   beat-in-bar 0xa6.
 */
djlink_err_t djlink_status_parse(const uint8_t *buf, size_t len, djlink_status_t *out)
{
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    if (buf == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (len < 0x24) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (memcmp(buf, DJLINK_MAGIC, DJLINK_MAGIC_LEN) != 0) {
        return DJLINK_ERR_MAGIC;
    }
    if (buf[0x0a] != DJLINK_TYPE_CDJ_STATUS) {
        return DJLINK_ERR_TYPE;
    }
    out->revision = buf[0x20]; /* 1 djm/rekordbox, 3 nxs, 4 xdj1000 */
    out->device_number = buf[0x21];
    out->lenr = djlink_rd16(&buf[0x22]);
    if (len >= 0x1f) {
        /* v246 fix: 0x0b..0x1e device name (20 bytes, 0x00 padded) was never
         * copied, leaving status.name empty for every parsed packet. */
        memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    }

    if (len >= 0x28) {
        out->active = djlink_rd16(&buf[0x26]) == 0x0001;
    }
    if (len >= 0x29) {
        out->source_device = buf[0x28];
    }
    if (len >= 0x2a) {
        out->source_slot = buf[0x29];
    }
    if (len >= 0x30) {
        out->rekordbox_id = djlink_rd32(&buf[0x2c]);
    }
    if (len >= 0x7c) {
        out->play_state = djlink_rd32(&buf[0x78]);
    }
    if (len >= 0x8a) {
        out->has_flag_bits = true;
        out->flags = buf[0x89];
    }
    if (len >= 0x90) {
        out->pitch_raw = (int32_t)djlink_rd32(&buf[0x8c]);
    }
    if (len >= 0x94) {
        out->bpm_state = djlink_rd16(&buf[0x90]);
        out->bpm100 = djlink_rd16(&buf[0x92]);
    }
    if (len >= 0x9f) {
        out->master_meaningful = buf[0x9e];
    }
    out->master_handoff = len >= 0xa0 ? buf[0x9f] : 0xffu;
    if (len >= 0xa4) {
        out->beat = djlink_rd32(&buf[0xa0]);
    }
    if (len >= 0xa7) {
        out->beat_in_bar = buf[0xa6];
    }
    return DJLINK_OK;
}

/* Build side of the same layout, nexus length (0xd4). Besides the parsed
 * fields: 0x1f 0x01, 0x2a track type (0x01 rekordbox when a track is
 * loaded), 0x7c firmware ("1.00"), 0x8b P2 (0x7a playing, 0x7e otherwise),
 * 0x94 0x7fffffff, 0x98 / 0xc0 / 0xc4 pitch copies, 0x9d P3 (0 no track,
 * 1 stopped, 9 playing), 0xa4 cue countdown
 * 0x01ff (none), 0xc8 packet counter. Everything else stays 0. */
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

int djlink_status_build(const djlink_status_t *in, uint32_t packet_counter,
                        uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_STATUS_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    const bool loaded = in->play_state != DJLINK_PLAY_NO_TRACK;
    const bool playing = (in->flags & DJLINK_FLAG_PLAYING) != 0u;
    memset(out, 0, DJLINK_STATUS_PACKET_LEN);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = DJLINK_TYPE_CDJ_STATUS;
    memcpy(&out[0x0b], in->name, DJLINK_NAME_LEN);
    out[0x1f] = 0x01;
    out[0x20] = 0x03;
    out[0x21] = in->device_number;
    wr16(&out[0x22], (uint16_t)(DJLINK_STATUS_PACKET_LEN - 0x24u));
    out[0x24] = in->device_number;
    wr16(&out[0x26], in->active ? 0x0001u : 0x0000u);
    out[0x28] = in->source_device;
    out[0x29] = in->source_slot;
    out[0x2a] = loaded ? 0x01u : 0x00u;
    djlink_wr32(&out[0x2c], in->rekordbox_id);
    djlink_wr32(&out[0x78], in->play_state);
    memcpy(&out[0x7c], "1.00", 4);
    out[0x89] = in->flags;
    out[0x8a] = 0xff;
    out[0x8b] = playing ? 0x7au : 0x7eu;
    djlink_wr32(&out[0x8c], (uint32_t)in->pitch_raw);
    wr16(&out[0x90], 0x8000u);
    wr16(&out[0x92], in->bpm100);
    djlink_wr32(&out[0x94], 0x7fffffffu);
    djlink_wr32(&out[0x98], (uint32_t)in->pitch_raw);
    out[0x9d] = !loaded ? 0x00u : (playing ? 0x09u : 0x01u);
    out[0x9e] = in->master_meaningful;
    out[0x9f] = in->master_handoff ? in->master_handoff : 0xffu; /* 0 is no player */
    djlink_wr32(&out[0xa0], in->beat);
    wr16(&out[0xa4], 0x01ffu);
    out[0xa6] = in->beat_in_bar;
    djlink_wr32(&out[0xc0], (uint32_t)in->pitch_raw);
    djlink_wr32(&out[0xc4], (uint32_t)in->pitch_raw);
    djlink_wr32(&out[0xc8], packet_counter);
    return (int)DJLINK_STATUS_PACKET_LEN;
}
