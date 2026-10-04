#include "djlink/sync.h"

#include <string.h>

static void write_port50001_header(uint8_t *out, uint8_t type, const uint8_t *name,
                                   uint8_t device_number, uint16_t lenr)
{
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = type;
    djlink_name_from_str((const char *)name, &out[0x0b]);
    out[0x1f] = 0x01;
    out[0x20] = 0x00;
    out[0x21] = device_number;
    out[0x22] = (uint8_t)(lenr >> 8);
    out[0x23] = (uint8_t)(lenr & 0xff);
}

static djlink_err_t parse_port50001(const uint8_t *buf, size_t len, uint8_t expected_type,
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

int djlink_sync_build(const djlink_sync_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_SYNC_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_SYNC_PACKET_LEN);
    write_port50001_header(out, 0x2a, in->name, in->sender_number, 0x08);
    out[0x27] = in->target_number;
    out[0x2b] = in->action;
    return (int)DJLINK_SYNC_PACKET_LEN;
}

djlink_err_t djlink_sync_parse(const uint8_t *buf, size_t len, djlink_sync_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = parse_port50001(buf, len, 0x2a, DJLINK_SYNC_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->sender_number = buf[0x21];
    out->target_number = buf[0x27];
    out->action = buf[0x2b];
    return DJLINK_OK;
}

int djlink_handoff_req_build(const djlink_handoff_req_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_HANDOFF_REQ_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_HANDOFF_REQ_PACKET_LEN);
    write_port50001_header(out, 0x26, (const uint8_t *)in->name, in->requester_number, 0x04);
    out[0x27] = in->requester_number;
    return (int)DJLINK_HANDOFF_REQ_PACKET_LEN;
}

djlink_err_t djlink_handoff_req_parse(const uint8_t *buf, size_t len, djlink_handoff_req_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = parse_port50001(buf, len, 0x26, DJLINK_HANDOFF_REQ_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    out->requester_number = buf[0x27];
    return DJLINK_OK;
}

int djlink_handoff_resp_build(const djlink_handoff_resp_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_HANDOFF_RESP_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_HANDOFF_RESP_PACKET_LEN);
    write_port50001_header(out, 0x27, (const uint8_t *)in->name, in->requester_number, 0x08);
    out[0x27] = in->requester_number;
    out[0x2b] = 0x01; /* acceptance */
    return (int)DJLINK_HANDOFF_RESP_PACKET_LEN;
}

djlink_err_t djlink_handoff_resp_parse(const uint8_t *buf, size_t len, djlink_handoff_resp_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = parse_port50001(buf, len, 0x27, DJLINK_HANDOFF_RESP_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    out->requester_number = buf[0x27];
    return DJLINK_OK;
}
