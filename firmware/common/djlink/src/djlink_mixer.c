#include "djlink/mixer.h"

#include <string.h>

static djlink_err_t check_packet(const uint8_t *buf, size_t len, uint8_t expected_type, size_t need)
{
    if (buf == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (len < need) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (memcmp(buf, DJLINK_MAGIC, DJLINK_MAGIC_LEN) != 0) {
        return DJLINK_ERR_MAGIC;
    }
    if (buf[0x0a] != expected_type) {
        return DJLINK_ERR_TYPE;
    }
    return DJLINK_OK;
}

static void write_beat_family_header(uint8_t *out, uint8_t type, const uint8_t *name,
                                     uint8_t device_number, uint8_t subtype_low)
{
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = type;
    djlink_name_from_str((const char *)name, &out[0x0b]);
    out[0x1f] = 0x01;
    out[0x20] = 0x00;
    out[0x21] = device_number;
    out[0x22] = 0x00;
    out[0x23] = subtype_low;
}

int djlink_fader_build(const djlink_fader_t *in, uint8_t *out, size_t cap)
{
    int i;
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_FADER_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_FADER_PACKET_LEN);
    write_beat_family_header(out, 0x02, in->name, in->player_number, 0x04);
    for (i = 0; i < 4; ++i) {
        out[0x24 + i] = (uint8_t)in->players[i];
    }
    return (int)DJLINK_FADER_PACKET_LEN;
}

djlink_err_t djlink_fader_parse(const uint8_t *buf, size_t len, djlink_fader_t *out)
{
    djlink_err_t err;
    int i;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = check_packet(buf, len, 0x02, DJLINK_FADER_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->player_number = buf[0x21];
    for (i = 0; i < 4; ++i) {
        out->players[i] = (djlink_fader_cmd_t)buf[0x24 + i];
    }
    return DJLINK_OK;
}

int djlink_onair_build(const djlink_onair_t *in, uint8_t *out, size_t cap)
{
    int i;
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_ONAIR_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_ONAIR_PACKET_LEN);
    write_beat_family_header(out, 0x03, in->name, in->mixer_number, 0x09);
    for (i = 0; i < 4; ++i) {
        out[0x24 + i] = in->on_air[i];
    }
    return (int)DJLINK_ONAIR_PACKET_LEN;
}

djlink_err_t djlink_onair_parse(const uint8_t *buf, size_t len, djlink_onair_t *out)
{
    djlink_err_t err;
    int i;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = check_packet(buf, len, 0x03, DJLINK_ONAIR_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->mixer_number = buf[0x21];
    for (i = 0; i < 4; ++i) {
        out->on_air[i] = buf[0x24 + i];
    }
    return DJLINK_OK;
}

djlink_err_t djlink_mixer_status_parse(const uint8_t *buf, size_t len,
                                       djlink_mixer_status_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = check_packet(buf, len, 0x29, DJLINK_MIXER_STATUS_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->device_number = buf[0x21];
    out->lenr = djlink_rd16(&buf[0x22]);
    out->flags = buf[0x27];
    out->pitch_raw = (int32_t)djlink_rd32(&buf[0x28]);
    out->bpm100 = djlink_rd16(&buf[0x2e]);
    out->master_handoff = buf[0x36];
    out->beat_in_bar = buf[0x37];
    return DJLINK_OK;
}

int djlink_load_track_build(const djlink_load_track_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_LOAD_TRACK_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_LOAD_TRACK_PACKET_LEN);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = 0x19;
    djlink_name_from_str(in->name, &out[0x0b]);
    out[0x1f] = 0x01;
    out[0x20] = 0x04; /* revision */
    out[0x21] = in->sender_number;
    out[0x22] = 0x00;
    out[0x23] = 0x34; /* lenr: total 0x58 - 0x24 */
    out[0x24] = in->sender_number;
    out[0x28] = in->target_player;
    out[0x29] = in->slot;
    out[0x2a] = 0x01; /* 0x0100 */
    out[0x2c] = (uint8_t)(in->rekordbox_id >> 24);
    out[0x2d] = (uint8_t)(in->rekordbox_id >> 16);
    out[0x2e] = (uint8_t)(in->rekordbox_id >> 8);
    out[0x2f] = (uint8_t)in->rekordbox_id;
    out[0x30] = 0x32;
    return (int)DJLINK_LOAD_TRACK_PACKET_LEN;
}

djlink_err_t djlink_load_track_parse(const uint8_t *buf, size_t len, djlink_load_track_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = check_packet(buf, len, 0x19, DJLINK_LOAD_TRACK_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    out->sender_number = buf[0x21];
    out->target_player = buf[0x28];
    out->slot = buf[0x29];
    out->rekordbox_id = djlink_rd32(&buf[0x2c]);
    return DJLINK_OK;
}

int djlink_load_ack_build(uint8_t device_number, const char *name, uint8_t *out, size_t cap)
{
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_LOAD_ACK_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_LOAD_ACK_PACKET_LEN);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = 0x1a;
    djlink_name_from_str(name, &out[0x0b]);
    out[0x1f] = 0x01;
    out[0x20] = 0x04;
    out[0x21] = device_number;
    out[0x22] = 0x00;
    out[0x23] = 0x02; /* lenr: 2 bytes of padding content */
    out[0x24] = device_number;
    return (int)DJLINK_LOAD_ACK_PACKET_LEN;
}

djlink_err_t djlink_load_ack_parse(const uint8_t *buf, size_t len, djlink_load_ack_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = check_packet(buf, len, 0x1a, DJLINK_LOAD_ACK_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    out->device_number = buf[0x21];
    return DJLINK_OK;
}
