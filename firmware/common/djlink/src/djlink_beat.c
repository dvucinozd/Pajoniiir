#include "djlink/beat.h"

#include <string.h>

/* Family header shared by beat / absolute-position packets:
 * magic, type, 20-byte name at 0x0b, u1=0x0100 at 0x1f, device number at
 * 0x21, 0x00 at 0x22, subtype byte at 0x23 (0x3c beat, 0x09 position —
 * written as the analysis' "lenr" low byte; readers should not require it). */
static void write_family_header(uint8_t *out, uint8_t type, const uint8_t name[DJLINK_NAME_LEN],
                                uint8_t device_number, uint8_t subtype)
{
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = type;
    memcpy(&out[0x0b], name, DJLINK_NAME_LEN);
    out[0x1f] = 0x01;
    out[0x20] = 0x00;
    out[0x21] = device_number;
    out[0x22] = 0x00;
    out[0x23] = subtype;
}

static djlink_err_t check_family_header(const uint8_t *buf, size_t len, uint8_t expected_type,
                                        size_t need)
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

djlink_err_t djlink_beat_parse(const uint8_t *buf, size_t len, djlink_beat_t *out)
{
    djlink_err_t err;

    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    if (buf == NULL) {
        return DJLINK_ERR_NULL;
    }
    err = check_family_header(buf, len, DJLINK_TYPE_BEAT, DJLINK_BEAT_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->device_number = buf[0x21];
    out->next_beat_ms = djlink_rd32(&buf[0x24]);
    out->second_beat_ms = djlink_rd32(&buf[0x28]);
    out->next_bar_ms = djlink_rd32(&buf[0x2c]);
    out->fourth_beat_ms = djlink_rd32(&buf[0x30]);
    out->second_bar_ms = djlink_rd32(&buf[0x34]);
    out->eighth_beat_ms = djlink_rd32(&buf[0x38]);
    out->pitch_raw = (int32_t)djlink_rd32(&buf[0x54]);
    out->bpm100 = djlink_rd16(&buf[0x5a]);
    out->beat_in_bar = buf[0x5c];
    return DJLINK_OK;
}

int djlink_beat_build(const djlink_beat_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_BEAT_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_BEAT_PACKET_LEN);
    write_family_header(out, DJLINK_TYPE_BEAT, in->name, in->device_number, 0x3c);
    djlink_wr32(&out[0x24], in->next_beat_ms);
    djlink_wr32(&out[0x28], in->second_beat_ms);
    djlink_wr32(&out[0x2c], in->next_bar_ms);
    djlink_wr32(&out[0x30], in->fourth_beat_ms);
    djlink_wr32(&out[0x34], in->second_bar_ms);
    djlink_wr32(&out[0x38], in->eighth_beat_ms);
    djlink_wr32(&out[0x54], (uint32_t)in->pitch_raw);
    out[0x5a] = (uint8_t)(in->bpm100 >> 8);
    out[0x5b] = (uint8_t)(in->bpm100 & 0xff);
    out[0x5c] = in->beat_in_bar;
    out[0x5f] = in->device_number; /* redundant copy the protocol expects */
    return (int)DJLINK_BEAT_PACKET_LEN;
}

djlink_err_t djlink_position_parse(const uint8_t *buf, size_t len, djlink_position_t *out)
{
    djlink_err_t err;

    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    if (buf == NULL) {
        return DJLINK_ERR_NULL;
    }
    err = check_family_header(buf, len, DJLINK_TYPE_ABS_POSITION, DJLINK_POSITION_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->device_number = buf[0x21];
    out->track_length_s = djlink_rd32(&buf[0x24]);
    out->playhead_ms = djlink_rd32(&buf[0x28]);
    out->pitch_x100 = (int32_t)djlink_rd32(&buf[0x2c]);
    out->bpm10 = (int32_t)djlink_rd32(&buf[0x38]);
    return DJLINK_OK;
}

int djlink_position_build(const djlink_position_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_POSITION_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_POSITION_PACKET_LEN);
    write_family_header(out, DJLINK_TYPE_ABS_POSITION, in->name, in->device_number, 0x09);
    djlink_wr32(&out[0x24], in->track_length_s);
    djlink_wr32(&out[0x28], in->playhead_ms);
    djlink_wr32(&out[0x2c], (uint32_t)in->pitch_x100);
    djlink_wr32(&out[0x38], (uint32_t)in->bpm10);
    return (int)DJLINK_POSITION_PACKET_LEN;
}
