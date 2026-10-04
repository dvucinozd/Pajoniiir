#include "djlink/media.h"

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

size_t djlink_utf16be_to_str(const uint8_t *src, size_t src_bytes, char *dst, size_t dst_len)
{
    size_t out = 0;
    size_t i;
    if (src == NULL || dst == NULL || dst_len == 0) {
        return 0;
    }
    for (i = 0; i + 1 < src_bytes && out + 1 < dst_len; i += 2) {
        uint16_t u = (uint16_t)((src[i] << 8) | src[i + 1]);
        if (u == 0) {
            break; /* NUL terminator / padding */
        }
        if (u < 0x80) {
            dst[out++] = (char)u;
        } else if (u < 0x800 && out + 2 < dst_len) {
            dst[out++] = (char)(0xc0 | (u >> 6));
            dst[out++] = (char)(0x80 | (u & 0x3f));
        } else if (out + 3 < dst_len) {
            dst[out++] = (char)(0xe0 | (u >> 12));
            dst[out++] = (char)(0x80 | ((u >> 6) & 0x3f));
            dst[out++] = (char)(0x80 | (u & 0x3f));
        } else {
            break;
        }
    }
    dst[out] = '\0';
    return out;
}

size_t djlink_str_to_utf16be(const char *src, uint8_t *dst, size_t field_size)
{
    size_t out = 0;
    const char *p = src;
    if (dst == NULL || field_size == 0) {
        return 0;
    }
    memset(dst, 0, field_size);
    if (src != NULL) {
        while (*p != '\0' && out + 2 <= field_size) {
            unsigned long u;
            const unsigned char c = (unsigned char)*p;
            if (c < 0x80) {
                u = c;
                p += 1;
            } else if ((c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
                u = ((unsigned long)(c & 0x1f) << 6) | (p[1] & 0x3f);
                p += 2;
            } else if ((c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
                u = ((unsigned long)(c & 0x0f) << 12) | ((unsigned long)(p[1] & 0x3f) << 6) |
                    (p[2] & 0x3f);
                p += 3;
            } else {
                u = '?'; /* unmappable byte: substitute */
                p += 1;
            }
            dst[out++] = (uint8_t)(u >> 8);
            dst[out++] = (uint8_t)(u & 0xff);
        }
    }
    return out;
}

int djlink_media_query_build(const djlink_media_query_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_MEDIA_QUERY_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_MEDIA_QUERY_PACKET_LEN);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = 0x05;
    djlink_name_from_str(in->name, &out[0x0b]);
    out[0x1f] = 0x01;
    out[0x20] = 0x00;
    out[0x21] = in->sender_number;
    out[0x22] = 0x00;
    out[0x23] = 0x0c; /* lenr */
    out[0x24] = (uint8_t)(in->reply_ip >> 24);
    out[0x25] = (uint8_t)(in->reply_ip >> 16);
    out[0x26] = (uint8_t)(in->reply_ip >> 8);
    out[0x27] = (uint8_t)in->reply_ip;
    out[0x2b] = in->source_device;
    out[0x2f] = in->slot;
    return (int)DJLINK_MEDIA_QUERY_PACKET_LEN;
}

djlink_err_t djlink_media_resp_parse(const uint8_t *buf, size_t len, djlink_media_resp_t *out)
{
    djlink_err_t err;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    err = check_packet(buf, len, 0x06, DJLINK_MEDIA_RESP_PACKET_LEN);
    if (err != DJLINK_OK) {
        return err;
    }
    memcpy(out->name, &buf[0x0b], DJLINK_NAME_LEN);
    out->device_number = buf[0x21];
    out->lenr = djlink_rd16(&buf[0x22]);
    out->source_device = buf[0x27];
    out->slot = buf[0x2b];
    djlink_utf16be_to_str(&buf[0x2c], 0x40, out->media_name, sizeof(out->media_name));
    djlink_utf16be_to_str(&buf[0x6c], 0x28, out->creation_date, sizeof(out->creation_date));
    out->track_count = djlink_rd16(&buf[0xa6]);
    out->color = buf[0xa8];
    out->track_type = buf[0xaa];
    out->my_settings = buf[0xab];
    out->playlist_count = djlink_rd16(&buf[0xae]);
    out->total_bytes = ((uint64_t)djlink_rd32(&buf[0xb0]) << 32) | djlink_rd32(&buf[0xb4]);
    out->free_bytes = ((uint64_t)djlink_rd32(&buf[0xb8]) << 32) | djlink_rd32(&buf[0xbc]);
    return DJLINK_OK;
}

int djlink_media_resp_build(const djlink_media_resp_t *in, uint8_t *out, size_t cap)
{
    if (in == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_MEDIA_RESP_PACKET_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    memset(out, 0, DJLINK_MEDIA_RESP_PACKET_LEN);
    memcpy(out, DJLINK_MAGIC, DJLINK_MAGIC_LEN);
    out[0x0a] = 0x06;
    memcpy(&out[0x0b], in->name, DJLINK_NAME_LEN);
    out[0x1f] = 0x01;
    out[0x20] = 0x00;
    out[0x21] = in->device_number;
    out[0x22] = 0x00;
    out[0x23] = 0x9c; /* lenr */
    out[0x27] = in->source_device;
    out[0x2b] = in->slot;
    djlink_str_to_utf16be(in->media_name, &out[0x2c], 0x40);
    djlink_str_to_utf16be(in->creation_date, &out[0x6c], 0x28);
    /* 0x94..0xa3: unknown UTF-16 text ("1000"), left zero. */
    out[0xa6] = (uint8_t)(in->track_count >> 8);
    out[0xa7] = (uint8_t)in->track_count;
    out[0xa8] = in->color;
    out[0xaa] = in->track_type;
    out[0xab] = in->my_settings;
    out[0xae] = (uint8_t)(in->playlist_count >> 8);
    out[0xaf] = (uint8_t)in->playlist_count;
    djlink_wr32(&out[0xb0], (uint32_t)(in->total_bytes >> 32));
    djlink_wr32(&out[0xb4], (uint32_t)in->total_bytes);
    djlink_wr32(&out[0xb8], (uint32_t)(in->free_bytes >> 32));
    djlink_wr32(&out[0xbc], (uint32_t)in->free_bytes);
    return (int)DJLINK_MEDIA_RESP_PACKET_LEN;
}
