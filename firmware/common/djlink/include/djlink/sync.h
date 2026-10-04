#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sync / tempo-master control packets (UDP port 50001, port-50001 family:
 * name at 0x0b, 0x01 at 0x1f, 0x00 at 0x20, device number at 0x21, lenr at
 * 0x22..0x23 BE). Layouts per sync.html.
 *
 * Sync control (type 0x2a, 0x2c bytes): content 000000 D (0x27), 000000 S
 * (0x2b). S: 0x10 = turn Sync on, 0x20 = leave Sync, 0x01 = become tempo
 * master (Master button equivalent). Per sync.html D is the sender's own
 * number, like 0x21: the command names no target, the receiving IP is it.
 */
#define DJLINK_SYNC_PACKET_LEN 0x2cu

#define DJLINK_SYNC_OFF    0x20u
#define DJLINK_SYNC_ON     0x10u
#define DJLINK_SYNC_MASTER 0x01u

typedef struct {
    uint8_t name[DJLINK_NAME_LEN]; /* sender device name */
    uint8_t sender_number;    /* D we are posing as (byte 0x21) */
    uint8_t target_number;    /* 0x27: the sender's number again (sync.html) */
    uint8_t action;           /* DJLINK_SYNC_* (0x2b) */
} djlink_sync_t;

int djlink_sync_build(const djlink_sync_t *in, uint8_t *out, size_t cap);
djlink_err_t djlink_sync_parse(const uint8_t *buf, size_t len, djlink_sync_t *out);

/* Tempo-master takeover request (type 0x26, 0x28 bytes): sent by the device
 * that wants to become master to the current master; content 000000 D at
 * 0x27 (the requester's number). */
#define DJLINK_HANDOFF_REQ_PACKET_LEN 0x28u

typedef struct {
    const char *name;
    uint8_t requester_number; /* D at 0x27 */
} djlink_handoff_req_t;

int djlink_handoff_req_build(const djlink_handoff_req_t *in, uint8_t *out, size_t cap);
djlink_err_t djlink_handoff_req_parse(const uint8_t *buf, size_t len,
                                      djlink_handoff_req_t *out);

/* Tempo-master takeover response (type 0x27, 0x2c bytes): the outgoing
 * master agrees; content 000000 D (0x27) + 00000001 (0x28..0x2b,
 * acceptance). Per sync.html D, like 0x21, is the responder's own number:
 * pass the outgoing master's number in requester_number. */
#define DJLINK_HANDOFF_RESP_PACKET_LEN 0x2cu

typedef struct {
    const char *name;
    uint8_t requester_number; /* D at 0x21 and 0x27: the responder (see above) */
} djlink_handoff_resp_t;

int djlink_handoff_resp_build(const djlink_handoff_resp_t *in, uint8_t *out, size_t cap);
djlink_err_t djlink_handoff_resp_parse(const uint8_t *buf, size_t len,
                                       djlink_handoff_resp_t *out);

#ifdef __cplusplus
}
#endif
