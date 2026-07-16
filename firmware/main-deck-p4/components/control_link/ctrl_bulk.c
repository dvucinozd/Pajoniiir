/* 0xA6 bulk frame codec: builders, streaming parser, and decoders.
 *
 * Pure C, no UART/ESP dependencies. This file is kept BYTE-FOR-BYTE IDENTICAL
 * on the S3 and P4 sides (the host runner asserts the two copies match) so the
 * link can never disagree on the wire format. Both sides build and decode all
 * frame types; each firmware only exercises the directions it needs. */

#include "control_link.h"

#include <string.h>

/* CRC16-CCITT: poly 0x1021, init 0xFFFF, no reflection, no final XOR. */
static uint16_t bulk_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            uint16_t shifted = (uint16_t)(crc << 1);
            crc = (crc & 0x8000u) != 0u
                ? (uint16_t)(shifted ^ (uint16_t)0x1021u)
                : shifted;
        }
    }
    return crc;
}

/* Finalise a frame whose header+payload occupy [0, CTRL_BULK_HEADER_LEN+len):
 * append the CRC16 over bytes [1, header+len) and return the total length. */
static size_t bulk_finalize(uint8_t *out, uint8_t type, uint8_t seq, uint8_t len)
{
    out[0] = CTRL_BULK_FRAME_START;
    out[1] = type;
    out[2] = seq;
    out[3] = len;
    uint16_t crc = bulk_crc16(out + 1, (size_t)(CTRL_BULK_HEADER_LEN - 1) + len);
    out[CTRL_BULK_HEADER_LEN + len] = (uint8_t)(crc & 0xFF);
    out[CTRL_BULK_HEADER_LEN + len + 1] = (uint8_t)(crc >> 8);
    return (size_t)CTRL_BULK_HEADER_LEN + len + CTRL_BULK_CRC_LEN;
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ── Builders ──────────────────────────────────────────────────────────────── */

size_t ctrl_bulk_build_descriptor_frame(uint8_t *out, size_t cap, uint8_t seq,
                                        const ctrl_descriptor_report_t *rep)
{
    const size_t frame_len =
        CTRL_BULK_HEADER_LEN + CTRL_DESC_PAYLOAD_LEN + CTRL_BULK_CRC_LEN;
    if (!out || !rep || cap < frame_len) {
        return 0;
    }
    uint8_t *p = out + CTRL_BULK_HEADER_LEN;
    wr_u16(p + 0, rep->vid);
    wr_u16(p + 2, rep->pid);
    wr_u16(p + 4, rep->caps);
    memset(p + 6, 0, CTRL_DESC_PRODUCT_MAX);
    for (size_t i = 0; i < CTRL_DESC_PRODUCT_MAX && rep->product[i] != '\0'; i++) {
        p[6 + i] = (uint8_t)rep->product[i];
    }
    return bulk_finalize(out, CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR, seq,
                         CTRL_DESC_PAYLOAD_LEN);
}

size_t ctrl_bulk_build_profile_begin(uint8_t *out, size_t cap, uint8_t seq,
                                     uint32_t total_size, uint32_t crc32,
                                     uint16_t vid, uint16_t pid)
{
    const size_t frame_len =
        CTRL_BULK_HEADER_LEN + CTRL_PROFILE_BEGIN_LEN + CTRL_BULK_CRC_LEN;
    if (!out || cap < frame_len) {
        return 0;
    }
    uint8_t *p = out + CTRL_BULK_HEADER_LEN;
    wr_u32(p + 0, total_size);
    wr_u32(p + 4, crc32);
    wr_u16(p + 8, vid);
    wr_u16(p + 10, pid);
    return bulk_finalize(out, CTRL_BULK_TYPE_PROFILE_BEGIN, seq,
                         CTRL_PROFILE_BEGIN_LEN);
}

size_t ctrl_bulk_build_profile_chunk(uint8_t *out, size_t cap, uint8_t seq,
                                     uint32_t offset, const uint8_t *data,
                                     size_t len)
{
    if (!out || !data || len == 0 || len > CTRL_PROFILE_CHUNK_MAX) {
        return 0;
    }
    const size_t payload = CTRL_PROFILE_CHUNK_HDR + len;
    const size_t frame_len = CTRL_BULK_HEADER_LEN + payload + CTRL_BULK_CRC_LEN;
    if (cap < frame_len) {
        return 0;
    }
    uint8_t *p = out + CTRL_BULK_HEADER_LEN;
    wr_u32(p + 0, offset);
    memcpy(p + CTRL_PROFILE_CHUNK_HDR, data, len);
    return bulk_finalize(out, CTRL_BULK_TYPE_PROFILE_CHUNK, seq, (uint8_t)payload);
}

size_t ctrl_bulk_build_profile_simple(uint8_t *out, size_t cap, uint8_t seq,
                                      uint8_t type)
{
    const size_t frame_len = CTRL_BULK_HEADER_LEN + CTRL_BULK_CRC_LEN;
    if (!out || cap < frame_len) {
        return 0;
    }
    return bulk_finalize(out, type, seq, 0);
}

size_t ctrl_bulk_build_profile_ack(uint8_t *out, size_t cap, uint8_t seq,
                                   uint8_t acked_type)
{
    const size_t frame_len = CTRL_BULK_HEADER_LEN + 1 + CTRL_BULK_CRC_LEN;
    if (!out || cap < frame_len) {
        return 0;
    }
    out[CTRL_BULK_HEADER_LEN] = acked_type;
    return bulk_finalize(out, CTRL_BULK_TYPE_PROFILE_ACK, seq, 1);
}

size_t ctrl_bulk_build_profile_nack(uint8_t *out, size_t cap, uint8_t seq,
                                    uint8_t nacked_type, uint8_t reason)
{
    const size_t frame_len = CTRL_BULK_HEADER_LEN + 2 + CTRL_BULK_CRC_LEN;
    if (!out || cap < frame_len) {
        return 0;
    }
    out[CTRL_BULK_HEADER_LEN] = nacked_type;
    out[CTRL_BULK_HEADER_LEN + 1] = reason;
    return bulk_finalize(out, CTRL_BULK_TYPE_PROFILE_NACK, seq, 2);
}

size_t ctrl_bulk_build_profile_status(uint8_t *out, size_t cap, uint8_t seq,
                                      uint8_t state, uint16_t vid, uint16_t pid)
{
    const size_t frame_len =
        CTRL_BULK_HEADER_LEN + CTRL_PROFILE_STATUS_LEN + CTRL_BULK_CRC_LEN;
    if (!out || cap < frame_len) {
        return 0;
    }
    uint8_t *p = out + CTRL_BULK_HEADER_LEN;
    p[0] = state;
    wr_u16(p + 1, vid);
    wr_u16(p + 3, pid);
    return bulk_finalize(out, CTRL_BULK_TYPE_PROFILE_STATUS, seq,
                         CTRL_PROFILE_STATUS_LEN);
}

size_t ctrl_bulk_build_firmware_report(uint8_t *out, size_t cap, uint8_t seq,
                                       const ctrl_firmware_report_t *rep)
{
    const size_t frame_len =
        CTRL_BULK_HEADER_LEN + CTRL_FW_REPORT_LEN + CTRL_BULK_CRC_LEN;
    if (!out || !rep || cap < frame_len) {
        return 0;
    }
    uint8_t *p = out + CTRL_BULK_HEADER_LEN;
    p[0] = rep->slot;
    p[1] = rep->state;
    memset(p + 2, 0, CTRL_FW_VERSION_MAX);
    for (size_t i = 0; i < CTRL_FW_VERSION_MAX && rep->version[i] != '\0'; i++) {
        p[2 + i] = (uint8_t)rep->version[i];
    }
    return bulk_finalize(out, CTRL_BULK_TYPE_FIRMWARE_REPORT, seq,
                         CTRL_FW_REPORT_LEN);
}

/* ── Streaming parser ──────────────────────────────────────────────────────── */

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
    size_t crc_span = (size_t)frame_len - 1 - CTRL_BULK_CRC_LEN;
    uint16_t expect = (uint16_t)(p->buf[frame_len - 2] |
                                 ((uint16_t)p->buf[frame_len - 1] << 8));
    uint16_t got = bulk_crc16(p->buf + 1, crc_span);
    ctrl_bulk_parser_reset(p);
    return (got == expect) ? frame_len : -1;
}

/* ── Decoders (frame already CRC/length validated by the parser) ───────────── */

static bool frame_type_len(const uint8_t *frame, size_t frame_len,
                           uint8_t type, uint8_t payload_len)
{
    const size_t want =
        (size_t)CTRL_BULK_HEADER_LEN + payload_len + CTRL_BULK_CRC_LEN;
    return frame && frame_len == want &&
           frame[0] == CTRL_BULK_FRAME_START && frame[1] == type &&
           frame[3] == payload_len;
}

bool ctrl_bulk_decode_descriptor(const uint8_t *frame, size_t frame_len,
                                 ctrl_descriptor_report_t *rep)
{
    if (!rep || !frame_type_len(frame, frame_len,
                                CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR,
                                CTRL_DESC_PAYLOAD_LEN)) {
        return false;
    }
    const uint8_t *p = frame + CTRL_BULK_HEADER_LEN;
    rep->vid = rd_u16(p + 0);
    rep->pid = rd_u16(p + 2);
    rep->caps = rd_u16(p + 4);
    memcpy(rep->product, p + 6, CTRL_DESC_PRODUCT_MAX);
    rep->product[CTRL_DESC_PRODUCT_MAX] = '\0';
    return true;
}

bool ctrl_bulk_decode_profile_begin(const uint8_t *frame, size_t frame_len,
                                    uint32_t *total_size, uint32_t *crc32,
                                    uint16_t *vid, uint16_t *pid)
{
    if (!total_size || !crc32 || !vid || !pid ||
        !frame_type_len(frame, frame_len, CTRL_BULK_TYPE_PROFILE_BEGIN,
                        CTRL_PROFILE_BEGIN_LEN)) {
        return false;
    }
    const uint8_t *p = frame + CTRL_BULK_HEADER_LEN;
    *total_size = rd_u32(p + 0);
    *crc32 = rd_u32(p + 4);
    *vid = rd_u16(p + 8);
    *pid = rd_u16(p + 10);
    return true;
}

bool ctrl_bulk_decode_profile_chunk(const uint8_t *frame, size_t frame_len,
                                    uint32_t *offset, const uint8_t **data,
                                    size_t *len)
{
    if (!offset || !data || !len || !frame ||
        frame[0] != CTRL_BULK_FRAME_START ||
        frame[1] != CTRL_BULK_TYPE_PROFILE_CHUNK) {
        return false;
    }
    uint8_t payload = frame[3];
    const size_t want =
        (size_t)CTRL_BULK_HEADER_LEN + payload + CTRL_BULK_CRC_LEN;
    if (frame_len != want || payload <= CTRL_PROFILE_CHUNK_HDR) {
        return false;
    }
    const uint8_t *p = frame + CTRL_BULK_HEADER_LEN;
    *offset = rd_u32(p + 0);
    *data = p + CTRL_PROFILE_CHUNK_HDR;
    *len = (size_t)payload - CTRL_PROFILE_CHUNK_HDR;
    return true;
}

bool ctrl_bulk_decode_profile_ack(const uint8_t *frame, size_t frame_len,
                                  uint8_t *acked_type)
{
    if (!acked_type ||
        !frame_type_len(frame, frame_len, CTRL_BULK_TYPE_PROFILE_ACK, 1)) {
        return false;
    }
    *acked_type = frame[CTRL_BULK_HEADER_LEN];
    return true;
}

bool ctrl_bulk_decode_profile_nack(const uint8_t *frame, size_t frame_len,
                                   uint8_t *nacked_type, uint8_t *reason)
{
    if (!nacked_type || !reason ||
        !frame_type_len(frame, frame_len, CTRL_BULK_TYPE_PROFILE_NACK, 2)) {
        return false;
    }
    *nacked_type = frame[CTRL_BULK_HEADER_LEN];
    *reason = frame[CTRL_BULK_HEADER_LEN + 1];
    return true;
}

bool ctrl_bulk_decode_profile_status(const uint8_t *frame, size_t frame_len,
                                     uint8_t *state, uint16_t *vid, uint16_t *pid)
{
    if (!state || !vid || !pid ||
        !frame_type_len(frame, frame_len, CTRL_BULK_TYPE_PROFILE_STATUS,
                        CTRL_PROFILE_STATUS_LEN)) {
        return false;
    }
    const uint8_t *p = frame + CTRL_BULK_HEADER_LEN;
    *state = p[0];
    *vid = rd_u16(p + 1);
    *pid = rd_u16(p + 3);
    return true;
}

bool ctrl_bulk_decode_firmware_report(const uint8_t *frame, size_t frame_len,
                                      ctrl_firmware_report_t *rep)
{
    if (!rep ||
        !frame_type_len(frame, frame_len, CTRL_BULK_TYPE_FIRMWARE_REPORT,
                        CTRL_FW_REPORT_LEN)) {
        return false;
    }
    const uint8_t *p = frame + CTRL_BULK_HEADER_LEN;
    rep->slot = p[0];
    rep->state = p[1];
    memcpy(rep->version, p + 2, CTRL_FW_VERSION_MAX);
    rep->version[CTRL_FW_VERSION_MAX] = '\0';
    return true;
}
