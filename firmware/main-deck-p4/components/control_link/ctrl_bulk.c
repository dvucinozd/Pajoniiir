/* 0xA6 bulk frame parser (P4 side). Pure C, no UART/ESP dependencies, so the
 * ctrl_bulk host test can round-trip frames from the S3 builder. */

#include "control_link.h"

#include <string.h>

/* CRC16-CCITT: poly 0x1021, init 0xFFFF, no reflection, no final XOR. */
static uint16_t bulk_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (uint16_t)((crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1);
        }
    }
    return crc;
}

void ctrl_bulk_parser_reset(ctrl_bulk_parser_t *p)
{
    if (p) {
        p->pos = 0;
        p->total_len = 0;
    }
}

int ctrl_bulk_parser_feed(ctrl_bulk_parser_t *p, uint8_t b)
{
    if (!p) {
        return -1;
    }

    if (p->pos == 0) {
        if (b != CTRL_BULK_FRAME_START) {
            return 0; /* not ours; caller keeps hunting */
        }
        p->buf[p->pos++] = b;
        p->total_len = 0;
        return 0;
    }

    p->buf[p->pos++] = b;

    if (p->pos == CTRL_BULK_HEADER_LEN) {
        uint8_t len = p->buf[3];
        if (len > CTRL_BULK_MAX_PAYLOAD) {
            ctrl_bulk_parser_reset(p);
            return -1;
        }
        p->total_len = CTRL_BULK_HEADER_LEN + (int)len + CTRL_BULK_CRC_LEN;
    }

    if (p->total_len == 0 || p->pos < p->total_len) {
        return 0;
    }

    int frame_len = p->total_len;
    ctrl_bulk_parser_reset(p);

    size_t crc_span = (size_t)frame_len - 1 - CTRL_BULK_CRC_LEN;
    uint16_t expect = (uint16_t)(p->buf[frame_len - 2] |
                                 ((uint16_t)p->buf[frame_len - 1] << 8));
    if (bulk_crc16(p->buf + 1, crc_span) != expect) {
        return -1;
    }
    return frame_len;
}

bool ctrl_bulk_decode_descriptor(const uint8_t *frame, size_t frame_len,
                                 ctrl_descriptor_report_t *rep)
{
    const size_t want =
        CTRL_BULK_HEADER_LEN + CTRL_DESC_PAYLOAD_LEN + CTRL_BULK_CRC_LEN;
    if (!frame || !rep || frame_len != want) {
        return false;
    }
    if (frame[0] != CTRL_BULK_FRAME_START ||
        frame[1] != CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR ||
        frame[3] != CTRL_DESC_PAYLOAD_LEN) {
        return false;
    }

    const uint8_t *p = frame + CTRL_BULK_HEADER_LEN;
    rep->vid = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    rep->pid = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
    rep->caps = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
    memcpy(rep->product, p + 6, CTRL_DESC_PRODUCT_MAX);
    rep->product[CTRL_DESC_PRODUCT_MAX] = '\0';
    return true;
}
