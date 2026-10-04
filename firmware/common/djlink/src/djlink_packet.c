#include "djlink/packet.h"

#include <string.h>

const uint8_t DJLINK_MAGIC[DJLINK_MAGIC_LEN] = {
    0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c,
};

/* All multi-byte values in DJ Link packets are BIG-endian (network byte
 * order), as confirmed by the DJ Link analysis byte equations and validated
 * implementations such as python-prodj-link. */

uint16_t djlink_rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t djlink_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void djlink_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

bool djlink_packet_is_valid(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < DJLINK_MAGIC_LEN + 1) {
        return false;
    }
    return memcmp(buf, DJLINK_MAGIC, DJLINK_MAGIC_LEN) == 0;
}

int djlink_packet_type(const uint8_t *buf, size_t len)
{
    if (!djlink_packet_is_valid(buf, len)) {
        return DJLINK_ERR_MAGIC;
    }
    return buf[DJLINK_MAGIC_LEN];
}

void djlink_name_to_str(const uint8_t name[DJLINK_NAME_LEN], char *dst)
{
    size_t i;
    if (name == NULL || dst == NULL) {
        return;
    }
    memcpy(dst, name, DJLINK_NAME_LEN);
    dst[DJLINK_NAME_LEN] = '\0';
    /* Trim trailing padding. */
    for (i = DJLINK_NAME_LEN; i > 0 && dst[i - 1] == '\0'; --i) {
        dst[i - 1] = '\0';
    }
}

void djlink_name_from_str(const char *src, uint8_t name[DJLINK_NAME_LEN])
{
    size_t n;
    if (name == NULL) {
        return;
    }
    memset(name, 0, DJLINK_NAME_LEN);
    if (src == NULL) {
        return;
    }
    n = strlen(src);
    if (n > DJLINK_NAME_LEN) {
        n = DJLINK_NAME_LEN;
    }
    memcpy(name, src, n);
}

float djlink_pitch_raw_to_percent(int32_t pitch_raw)
{
    /* (pitch_raw / 0x100000 - 1) * 100 */
    return ((float)pitch_raw / (float)0x100000 - 1.0f) * 100.0f;
}

int32_t djlink_pitch_percent_to_raw(float percent)
{
    return (int32_t)((percent / 100.0f + 1.0f) * (float)0x100000);
}

float djlink_effective_bpm(uint16_t bpm100, int32_t pitch_raw)
{
    return (float)bpm100 * (float)pitch_raw / (float)0x100000 / 100.0f;
}
