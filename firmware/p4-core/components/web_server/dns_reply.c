#include "dns_reply.h"

#include <string.h>

#define DNS_HEADER_LEN 12u
#define DNS_QR_RESPONSE 0x8000u
#define DNS_FLAG_AA 0x0400u
#define DNS_FLAG_RD 0x0100u
#define DNS_TYPE_A 1u
#define DNS_CLASS_IN 1u
#define DNS_TTL_SECONDS 60u
#define DNS_ANSWER_LEN 16u

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int find_question_end(const uint8_t *query, size_t query_len, size_t *question_end)
{
    size_t pos = DNS_HEADER_LEN;

    while (pos < query_len) {
        uint8_t label_len = query[pos];
        if (label_len == 0) {
            pos++;
            if (pos + 4u > query_len) {
                return 0;
            }
            *question_end = pos + 4u;
            return 1;
        }
        if ((label_len & 0xc0u) != 0 || label_len > 63u) {
            return 0;
        }
        pos++;
        if (pos + label_len > query_len) {
            return 0;
        }
        pos += label_len;
    }

    return 0;
}

size_t dns_build_captive_reply(const uint8_t *query,
                               size_t query_len,
                               uint8_t *reply,
                               size_t reply_size,
                               const uint8_t ip[4])
{
    size_t question_end = 0;

    if (!query || !reply || !ip || query_len < DNS_HEADER_LEN) {
        return 0;
    }
    if ((read_be16(&query[2]) & DNS_QR_RESPONSE) != 0) {
        return 0;
    }
    if (read_be16(&query[4]) == 0) {
        return 0;
    }
    if (!find_question_end(query, query_len, &question_end)) {
        return 0;
    }

    size_t reply_len = question_end + DNS_ANSWER_LEN;
    if (reply_len > reply_size) {
        return 0;
    }

    memset(reply, 0, reply_size);
    memcpy(reply, query, question_end);

    uint16_t request_flags = read_be16(&query[2]);
    write_be16(&reply[2], DNS_QR_RESPONSE | DNS_FLAG_AA | (request_flags & DNS_FLAG_RD));
    write_be16(&reply[4], 1);
    write_be16(&reply[6], 1);
    write_be16(&reply[8], 0);
    write_be16(&reply[10], 0);

    size_t pos = question_end;
    reply[pos++] = 0xc0;
    reply[pos++] = (uint8_t)DNS_HEADER_LEN;
    write_be16(&reply[pos], DNS_TYPE_A);
    pos += 2;
    write_be16(&reply[pos], DNS_CLASS_IN);
    pos += 2;
    write_be32(&reply[pos], DNS_TTL_SECONDS);
    pos += 4;
    write_be16(&reply[pos], 4);
    pos += 2;
    memcpy(&reply[pos], ip, 4);
    pos += 4;

    return pos;
}
