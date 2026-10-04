#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Remote DB server protocol (TCP). Discovered via the DJ Link analysis
 * (track_metadata.html) and cross-checked with python-prodj-link
 * (DBServerQuery / DBMessage / DBField structs).
 *
 * Flow: TCP connect to port 12523 of the player -> send the port query ->
 * read a 2-byte big-endian reply port -> connect to that port -> send the
 * setup greeting (int32 field 1) -> send query-context setup message ->
 * metadata/render requests and replies. All integers big-endian here
 * (unlike ANLZ files, whose beat-grid times are little-endian).
 */

#define DJLINK_DBSERVER_DISCOVERY_PORT 12523u

/* Port query: 00 00 00 0f "RemoteDBServer" 00. Reply: u16 BE port. */
#define DJLINK_DB_PORT_QUERY_LEN 19u

int djlink_db_port_query_build(uint8_t *out, size_t cap);
djlink_err_t djlink_db_port_query_reply_parse(const uint8_t *buf, size_t len, uint16_t *port);

/* Setup greeting: int32 field with value 1 (the server echoes it back). */
#define DJLINK_DB_SETUP_LEN 5u

int djlink_db_setup_build(uint8_t *out, size_t cap);

/* --- Typed fields -------------------------------------------------------- */
#define DJLINK_DB_FIELD_INT8   0x0fu
#define DJLINK_DB_FIELD_INT16  0x10u
#define DJLINK_DB_FIELD_INT32  0x11u
#define DJLINK_DB_FIELD_BINARY 0x14u
#define DJLINK_DB_FIELD_STRING 0x26u

/* --- Messages ------------------------------------------------------------ */
#define DJLINK_DB_MAGIC       0x872349aeu
#define DJLINK_DB_MAX_ARGS    12u
#define DJLINK_DB_SETUP_TXID  0xfffffffeu

typedef struct {
    uint8_t type;      /* DJLINK_DB_FIELD_* */
    uint32_t num;      /* INT8/16/32 value */
    const uint8_t *bin; /* BINARY payload or STRING as UTF-16BE bytes */
    size_t bin_len;    /* byte length of bin (even for a STRING, whose wire
                        * length field counts UTF-16 code units) */
} djlink_db_arg_t;

typedef struct {
    uint32_t txid;
    uint16_t type;
    uint8_t arg_count;
    djlink_db_arg_t args[DJLINK_DB_MAX_ARGS]; /* bin points into the input
                                               * buffer, valid while it does */
} djlink_db_msg_t;

/* Message type codes (request / reply). */
#define DJLINK_DB_TYPE_SETUP             0x0000u
#define DJLINK_DB_TYPE_METADATA_REQUEST  0x2002u
#define DJLINK_DB_TYPE_ARTWORK_REQUEST   0x2003u
#define DJLINK_DB_TYPE_PREVIEW_WAVEFORM  0x2004u
#define DJLINK_DB_TYPE_TRACK_INFO_REQUEST 0x2202u /* non-rekordbox tracks */
#define DJLINK_DB_TYPE_BEATGRID_REQUEST  0x2204u
#define DJLINK_DB_TYPE_CUES_REQUEST      0x2104u
#define DJLINK_DB_TYPE_WAVEFORM_REQUEST  0x2904u
#define DJLINK_DB_TYPE_CUES_EXT_REQUEST  0x2b04u /* nxs2 cue/loop list */
#define DJLINK_DB_TYPE_RENDER            0x3000u
#define DJLINK_DB_TYPE_SUCCESS           0x4000u
#define DJLINK_DB_TYPE_MENU_HEADER       0x4001u
#define DJLINK_DB_TYPE_ARTWORK           0x4002u
#define DJLINK_DB_TYPE_MENU_ITEM         0x4101u
#define DJLINK_DB_TYPE_MENU_FOOTER       0x4201u
#define DJLINK_DB_TYPE_PREVIEW_WAVEFORM_REPLY 0x4402u
#define DJLINK_DB_TYPE_BEATGRID_REPLY    0x4602u
#define DJLINK_DB_TYPE_CUES_REPLY        0x4702u
#define DJLINK_DB_TYPE_WAVEFORM_REPLY    0x4a02u
#define DJLINK_DB_TYPE_CUES_EXT_REPLY    0x4e02u

/* Build one message. Returns byte count or negative DJLINK_ERR_*. */
int djlink_db_msg_build(uint32_t txid, uint16_t type, const djlink_db_arg_t *args,
                        uint8_t arg_count, uint8_t *out, size_t cap);

/* Parse one message. arg bin pointers reference buf. Returns DJLINK_OK or
 * DJLINK_ERR_TRUNCATED / _MAGIC / _BOUNDS (arg type unknown). */
djlink_err_t djlink_db_msg_parse(const uint8_t *buf, size_t len, djlink_db_msg_t *out);
/* TCP may fragment/coalesce messages. On success consumed is the exact first
 * message size; on failure it is zero. Borrowed pointers reference buf. */
djlink_err_t djlink_db_msg_parse_prefix(const uint8_t *buf, size_t len,
                                       djlink_db_msg_t *out, size_t *consumed);

/* --- Ready-made requests -------------------------------------------------- */

/* Query-context setup: type 0, txid 0xfffffffe, single int32 arg D (our
 * player number, 1..4, must exist on the network and differ from the peer). */
int djlink_db_context_setup_build(uint8_t device_number, uint8_t *out, size_t cap);

/* Rekordbox track metadata request: DMST int32 (D, menu=1, slot, track
 * type=1) + rekordbox database ID. */
int djlink_db_metadata_request_build(uint32_t txid, uint8_t device_number, uint8_t slot,
                                     uint32_t rekordbox_id, uint8_t *out, size_t cap);

/* Render request for a menu established by a request: DMST + offset +
 * limit + 0 + len_a(=limit) + 0. */
int djlink_db_render_request_build(uint32_t txid, uint8_t device_number, uint8_t slot,
                                   uint32_t offset, uint32_t limit, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
