/* 0xA6 bulk frame builder (S3 side). Pure C, no UART/ESP dependencies, so the
 * ctrl_bulk host test can round-trip frames against the P4 parser. */

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

size_t ctrl_bulk_build_descriptor_frame(uint8_t *out, size_t cap, uint8_t seq,
                                        const ctrl_descriptor_report_t *rep)
{
    const size_t frame_len =
        CTRL_BULK_HEADER_LEN + CTRL_DESC_PAYLOAD_LEN + CTRL_BULK_CRC_LEN;
    if (!out || !rep || cap < frame_len) {
        return 0;
    }

    out[0] = CTRL_BULK_FRAME_START;
    out[1] = CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR;
    out[2] = seq;
    out[3] = CTRL_DESC_PAYLOAD_LEN;

    uint8_t *p = out + CTRL_BULK_HEADER_LEN;
    p[0] = (uint8_t)(rep->vid & 0xFF);
    p[1] = (uint8_t)(rep->vid >> 8);
    p[2] = (uint8_t)(rep->pid & 0xFF);
    p[3] = (uint8_t)(rep->pid >> 8);
    p[4] = (uint8_t)(rep->caps & 0xFF);
    p[5] = (uint8_t)(rep->caps >> 8);
    memset(p + 6, 0, CTRL_DESC_PRODUCT_MAX);
    /* NUL-padded, deliberately truncated to the wire field width. */
    for (size_t i = 0; i < CTRL_DESC_PRODUCT_MAX && rep->product[i] != '\0'; i++) {
        p[6 + i] = (uint8_t)rep->product[i];
    }

    uint16_t crc = bulk_crc16(out + 1,
                              (size_t)CTRL_BULK_HEADER_LEN - 1 + CTRL_DESC_PAYLOAD_LEN);
    out[CTRL_BULK_HEADER_LEN + CTRL_DESC_PAYLOAD_LEN] = (uint8_t)(crc & 0xFF);
    out[CTRL_BULK_HEADER_LEN + CTRL_DESC_PAYLOAD_LEN + 1] = (uint8_t)(crc >> 8);
    return frame_len;
}
