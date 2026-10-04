#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Channel-number claim / announcement packets (UDP port 50000), used to
 * join the network as a specific player number. Layouts follow the DJ Link
 * analysis (startup.html) cross-checked with python-prodj-link:
 *   0x00 magic, 0x0a type, 0x0b padding, 0x0c..0x1f device name,
 *   0x20 0x01, 0x21 device type (1 = mixer, 2 = CDJ), 0x22 padding,
 *   0x23 subtype, content from 0x24.
 */

#define DJLINK_DEVICE_TYPE_MIXER 0x01u
#define DJLINK_DEVICE_TYPE_CDJ   0x02u

/* Initial announcement (type 0x0a, 0x25 bytes). Broadcast 3x at ~300 ms
 * when joining. The final byte differs: 0x01 seen from CDJs, 0x02/0x03 from
 * mixers (python-prodj-link: cdj 1, djm 3). */
#define DJLINK_ANNOUNCE_PACKET_LEN 0x25u

typedef struct {
    const char *name;
    uint8_t device_type;  /* DJLINK_DEVICE_TYPE_* */
    uint8_t payload_byte; /* byte 0x24: 0x01 CDJ, 0x02 mixer (per analysis) */
} djlink_announce_t;

int djlink_announce_build(const djlink_announce_t *in, uint8_t *out, size_t cap);

/* First-stage claim (type 0x00, 0x2c bytes): publishes our MAC, iteration
 * N = 1..3, sent 3x at ~300 ms. */
#define DJLINK_CLAIM_MAC_PACKET_LEN 0x2cu

typedef struct {
    const char *name;
    uint8_t device_type;
    uint8_t iteration; /* 1..3 */
    uint8_t mac[6];
} djlink_claim_mac_t;

int djlink_claim_mac_build(const djlink_claim_mac_t *in, uint8_t *out, size_t cap);

/* Second-stage claim (type 0x02, 0x32 bytes): publishes IP + MAC + wanted
 * device number. Byte 0x31 (assignment) is 0x01 auto-assign, 0x02 specific
 * number. */
#define DJLINK_CLAIM_IP_PACKET_LEN 0x32u

typedef struct {
    const char *name;
    uint8_t device_type;
    uint32_t ip;           /* BE on the wire */
    uint8_t mac[6];
    uint8_t device_number; /* wanted player number D */
    uint8_t iteration;     /* 1..3 */
    uint8_t auto_assign;   /* 1 = auto, 2 = specific number */
} djlink_claim_ip_t;

int djlink_claim_ip_build(const djlink_claim_ip_t *in, uint8_t *out, size_t cap);

/* Final-stage claim (type 0x04): D at 0x24, iteration at 0x25 (N = 1..3).
 * The analysis reports a 0x2a-byte packet; the four trailing bytes are not
 * meaningfully identified and are sent as zero here. */
#define DJLINK_CLAIM_FINAL_PACKET_LEN 0x2au

typedef struct {
    const char *name;
    uint8_t device_type;
    uint8_t device_number;
    uint8_t iteration; /* 1..3 */
} djlink_claim_final_t;

int djlink_claim_final_build(const djlink_claim_final_t *in, uint8_t *out, size_t cap);

/* Channel conflict (type 0x08): sent when a device sees another claiming
 * its number. Parse only: reports the defended device number (byte 0x24). */
typedef struct {
    uint8_t device_number;
} djlink_conflict_t;

djlink_err_t djlink_conflict_parse(const uint8_t *buf, size_t len, djlink_conflict_t *out);

/* Assignment finished (type 0x05): unicast by an already-settled device
 * (mixer byte 0x21 style, or a settled CDJ carrying its own number) to cut
 * a newcomer's claim series short. Minimal form: D at 0x24. */
#define DJLINK_ASSIGN_FINISHED_PACKET_LEN 0x25u

typedef struct {
    const char *name;
    uint8_t device_number;
} djlink_assign_finished_t;

int djlink_assign_finished_build(const djlink_assign_finished_t *in, uint8_t *out, size_t cap);

/* Generic parse for claim-family packets: extracts device name, device
 * number (0 for announcement/stage-1) and iteration (0 when absent). */
typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t device_type; /* byte 0x21 */
    uint8_t device_number;
    uint8_t iteration;
} djlink_claim_info_t;

djlink_err_t djlink_claim_info_parse(const uint8_t *buf, size_t len, djlink_claim_info_t *out);

#ifdef __cplusplus
}
#endif
