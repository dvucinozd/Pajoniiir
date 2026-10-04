#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Media slot query / response (UDP port 50002). Layouts per media.html.
 * Both use the port-50002 family header: name 0x0b..0x1e, 0x01 at 0x1f,
 * 0x00 at 0x20, device number 0x21, lenr 0x22..0x23 BE.
 */

/* Media query (type 0x05, 0x30 bytes): own IP at 0x24 (the reply is unicast
 * to it), zeros, source device Dr at 0x2b, zeros, slot Sr at 0x2f.
 * lenr = 0x0c. */
#define DJLINK_MEDIA_QUERY_PACKET_LEN 0x30u

typedef struct {
    const char *name;      /* our device name */
    uint8_t sender_number; /* D we are posing as */
    uint32_t reply_ip;     /* our interface address (host order) */
    uint8_t source_device; /* Dr: device holding the slot */
    uint8_t slot;          /* Sr: DJLINK_SLOT_* */
} djlink_media_query_t;

int djlink_media_query_build(const djlink_media_query_t *in, uint8_t *out, size_t cap);

/* Media response (type 0x06, 0xc0 bytes; also the unsolicited broadcast
 * format). lenr = 0x9c. */
#define DJLINK_MEDIA_RESP_PACKET_LEN 0xc0u

typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t device_number;
    uint16_t lenr;
    uint8_t source_device; /* 0x27 */
    uint8_t slot;          /* 0x2b */
    char media_name[21];   /* UTF-16BE at 0x2c, 40 bytes, NUL-padded */
    char creation_date[13];/* UTF-16BE at 0x6c, 24 bytes */
    uint16_t track_count;  /* 0xa6 */
    uint8_t color;         /* 0xa8: DJLINK_MEDIA_COLOR_* */
    uint8_t track_type;    /* 0xaa: 1 rekordbox, 2 unanalyzed */
    uint8_t my_settings;   /* 0xab: non-zero = My Settings file present */
    uint16_t playlist_count; /* 0xae */
    uint64_t total_bytes;  /* 0xb0 */
    uint64_t free_bytes;   /* 0xb8 */
} djlink_media_resp_t;

djlink_err_t djlink_media_resp_parse(const uint8_t *buf, size_t len, djlink_media_resp_t *out);

/* Builds a media response (e.g. for tests or a virtual media slot);
 * returns byte count or negative DJLINK_ERR_*. */
int djlink_media_resp_build(const djlink_media_resp_t *in, uint8_t *out, size_t cap);

/* Media UI tint colors (byte 0xa8). */
#define DJLINK_MEDIA_COLOR_DEFAULT 0x00u
#define DJLINK_MEDIA_COLOR_PINK    0x01u
#define DJLINK_MEDIA_COLOR_RED     0x02u
#define DJLINK_MEDIA_COLOR_ORANGE  0x03u
#define DJLINK_MEDIA_COLOR_YELLOW  0x04u
#define DJLINK_MEDIA_COLOR_GREEN   0x05u
#define DJLINK_MEDIA_COLOR_AQUA    0x06u
#define DJLINK_MEDIA_COLOR_BLUE    0x07u
#define DJLINK_MEDIA_COLOR_PURPLE  0x08u

/* Decode a big-endian UTF-16 field (max out_len-1 chars) into a UTF-8 C
 * string (BMP only, NUL terminator added). Returns chars written. */
size_t djlink_utf16be_to_str(const uint8_t *src, size_t src_bytes, char *dst, size_t dst_len);

/* Encode a UTF-8 C string (BMP only) into a fixed-size big-endian UTF-16
 * field, NUL-padded. Returns bytes written (field_size). */
size_t djlink_str_to_utf16be(const char *src, uint8_t *dst, size_t field_size);

#ifdef __cplusplus
}
#endif
