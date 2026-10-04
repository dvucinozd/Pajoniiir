#include "djlink/claim.h"

#include <string.h>

/* Shared 50000-family header: magic, type, pad, 20-byte name at 0x0c,
 * 0x01 at 0x20, device type at 0x21, pad at 0x22, subtype at 0x23. */
static void write_port50000_header(uint8_t *out, uint8_t type, const char *name,
                                   uint8_t device_type, uint8_t subtype)
{
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = type;
    out[0x0b] = 0x00;
    djlink_name_from_str(name, &out[0x0c]);
    out[0x20] = 0x01;
    out[0x21] = device_type;
    out[0x22] = 0x00;
    out[0x23] = subtype;
}

int djlink_announce_build(const djlink_announce_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_ANNOUNCE_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_ANNOUNCE_PACKET_LEN);
    write_port50000_header(out, 0x0a, in->name, in->device_type, 0x02);
    out[0x24] = in->payload_byte;
    return (int)DJLINK_ANNOUNCE_PACKET_LEN;
}

int djlink_claim_mac_build(const djlink_claim_mac_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_CLAIM_MAC_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_CLAIM_MAC_PACKET_LEN);
    write_port50000_header(out, 0x00, in->name, in->device_type, 0x02);
    out[0x24] = in->iteration;
    out[0x25] = 0x01; /* status feature flags: player */
    memcpy(&out[0x26], in->mac, 6);
    return (int)DJLINK_CLAIM_MAC_PACKET_LEN;
}

int djlink_claim_ip_build(const djlink_claim_ip_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_CLAIM_IP_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_CLAIM_IP_PACKET_LEN);
    write_port50000_header(out, 0x02, in->name, in->device_type, 0x02);
    out[0x24] = (uint8_t)(in->ip >> 24);
    out[0x25] = (uint8_t)(in->ip >> 16);
    out[0x26] = (uint8_t)(in->ip >> 8);
    out[0x27] = (uint8_t)in->ip;
    memcpy(&out[0x28], in->mac, 6);
    out[0x2e] = in->device_number;
    out[0x2f] = in->iteration;
    out[0x30] = 0x01; /* flags: player */
    out[0x31] = in->auto_assign ? in->auto_assign : 0x02;
    return (int)DJLINK_CLAIM_IP_PACKET_LEN;
}

int djlink_claim_final_build(const djlink_claim_final_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_CLAIM_FINAL_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_CLAIM_FINAL_PACKET_LEN);
    write_port50000_header(out, 0x04, in->name, in->device_type, 0x02);
    out[0x24] = in->device_number;
    out[0x25] = in->iteration;
    /* 0x26..0x29: present in the 0x2a-byte captures but not meaningfully
     * identified; sent as zero. */
    return (int)DJLINK_CLAIM_FINAL_PACKET_LEN;
}

int djlink_assign_finished_build(const djlink_assign_finished_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_ASSIGN_FINISHED_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_ASSIGN_FINISHED_PACKET_LEN);
    write_port50000_header(out, 0x05, in->name, DJLINK_DEVICE_TYPE_CDJ, 0x02);
    out[0x24] = in->device_number;
    return (int)DJLINK_ASSIGN_FINISHED_PACKET_LEN;
}

djlink_err_t djlink_conflict_parse(const uint8_t *buf, size_t len, djlink_conflict_t *out)
{
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    if (buf == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (len < 0x25) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (memcmp(buf, DJLINK_MAGIC, DJLINK_MAGIC_LEN) != 0) {
        return DJLINK_ERR_MAGIC;
    }
    if (buf[0x0a] != 0x08) {
        return DJLINK_ERR_TYPE;
    }
    out->device_number = buf[0x24];
    return DJLINK_OK;
}

djlink_err_t djlink_claim_info_parse(const uint8_t *buf, size_t len, djlink_claim_info_t *out)
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
    memcpy(out->name, &buf[0x0c], DJLINK_NAME_LEN);
    out->device_type = buf[0x21];
    switch (buf[0x0a]) {
    case 0x00: /* stage 1: iteration at 0x24, no device number yet */
        if (len >= 0x25) {
            out->iteration = buf[0x24];
        }
        break;
    case 0x02: /* stage 2: D at 0x2e, iteration at 0x2f */
        if (len >= 0x30) {
            out->device_number = buf[0x2e];
            out->iteration = buf[0x2f];
        }
        break;
    case 0x04:
    case 0x05:
    case 0x08: /* D at 0x24; final claim also carries iteration at 0x25 */
        if (len >= 0x25) {
            out->device_number = buf[0x24];
            if (buf[0x0a] == 0x04 && len >= 0x26) {
                out->iteration = buf[0x25];
            }
        }
        break;
    default: /* 0x0a announcement: neither field present */
        break;
    }
    return DJLINK_OK;
}
