/* Controller-profile transfer receiver (S3 role).
 *
 * Pure C, no UART/ESP dependencies. Kept BYTE-FOR-BYTE IDENTICAL on the S3 and
 * P4 sides (the host runner asserts the two copies match); the P4 only uses
 * cp_xfer_crc32() to stamp the transfer, while the S3 runs the full receiver. */

#include "control_link.h"

#include <string.h>

uint32_t cp_xfer_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void cp_xfer_rx_init(cp_xfer_rx_t *rx, uint8_t *buf, size_t cap)
{
    if (!rx) {
        return;
    }
    memset(rx, 0, sizeof(*rx));
    rx->buf = buf;
    rx->cap = cap;
    rx->state = CTRL_PROFILE_STATE_IDLE;
}

uint8_t cp_xfer_rx_begin(cp_xfer_rx_t *rx, uint32_t total_size, uint32_t crc32,
                         uint16_t vid, uint16_t pid)
{
    if (!rx) {
        return CTRL_PROFILE_NACK_STATE;
    }
    if (total_size == 0 || !rx->buf || total_size > rx->cap) {
        rx->state = CTRL_PROFILE_STATE_ERROR;
        return CTRL_PROFILE_NACK_SIZE;
    }
    rx->total = total_size;
    rx->crc = crc32;
    rx->vid = vid;
    rx->pid = pid;
    rx->received = 0;
    rx->state = CTRL_PROFILE_STATE_RECEIVING;
    return CTRL_PROFILE_NACK_NONE;
}

uint8_t cp_xfer_rx_chunk(cp_xfer_rx_t *rx, uint32_t offset,
                         const uint8_t *data, size_t len)
{
    if (!rx || rx->state != CTRL_PROFILE_STATE_RECEIVING) {
        return CTRL_PROFILE_NACK_STATE;
    }
    if (!data || len == 0 || offset != rx->received ||
        offset + (uint32_t)len > rx->total) {
        rx->state = CTRL_PROFILE_STATE_ERROR;
        return CTRL_PROFILE_NACK_OFFSET;
    }
    memcpy(rx->buf + offset, data, len);
    rx->received += (uint32_t)len;
    return CTRL_PROFILE_NACK_NONE;
}

uint8_t cp_xfer_rx_end(cp_xfer_rx_t *rx)
{
    if (!rx || rx->state != CTRL_PROFILE_STATE_RECEIVING) {
        return CTRL_PROFILE_NACK_STATE;
    }
    if (rx->received != rx->total ||
        cp_xfer_crc32(rx->buf, rx->total) != rx->crc) {
        rx->state = CTRL_PROFILE_STATE_ERROR;
        return CTRL_PROFILE_NACK_CRC;
    }
    rx->state = CTRL_PROFILE_STATE_STORED;
    return CTRL_PROFILE_NACK_NONE;
}
