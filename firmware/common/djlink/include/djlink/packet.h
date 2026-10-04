#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pioneer Pro DJ Link common framing.
 *
 * Protocol knowledge learned from the Deep Symmetry "DJ Link Ecosystem
 * Analysis" (https://djl-analysis.deepsymmetry.org/djl-analysis/) — no code
 * from that project is used here. Cross-checked against validated open
 * implementations (python-prodj-link). All multi-byte values are
 * BIG-endian. Byte offsets in comments are the ones used in that document,
 * hexadecimal.
 */

#define DJLINK_MAGIC_LEN 10

/* "Qspt1WmJOL" — fixed 10-byte preamble of every DJ Link packet. */
extern const uint8_t DJLINK_MAGIC[DJLINK_MAGIC_LEN];

/* UDP ports used by the protocol. */
#define DJLINK_PORT_DISCOVERY 50000u /* keep-alive, channel claims */
#define DJLINK_PORT_BEAT      50001u /* beat, absolute position, sync */
#define DJLINK_PORT_STATUS    50002u /* CDJ/mixer status, media, load track */

/* Packet type bytes (byte 0x0a). */
#define DJLINK_TYPE_BEAT         0x28u /* port 50001, 0x60 bytes */
#define DJLINK_TYPE_ABS_POSITION 0x0bu /* port 50001, 0x3c bytes */
#define DJLINK_TYPE_CDJ_STATUS   0x0au /* port 50002 */
#define DJLINK_TYPE_KEEPALIVE    0x06u /* port 50000, 0x36 bytes */

#define DJLINK_MAX_PACKET 512
#define DJLINK_NAME_LEN   20 /* device name field, padded with 0x00 */

typedef enum {
    DJLINK_OK = 0,
    DJLINK_ERR_NULL = -1,
    DJLINK_ERR_BOUNDS = -2,   /* buffer too short for claimed structure */
    DJLINK_ERR_MAGIC = -3,    /* missing/mismatched magic header */
    DJLINK_ERR_TYPE = -4,     /* unexpected packet type */
    DJLINK_ERR_TRUNCATED = -5 /* packet shorter than its fixed layout */
} djlink_err_t;

/* Minimum sanity check: correct magic and plausible length. */
bool djlink_packet_is_valid(const uint8_t *buf, size_t len);

/* Cheap dispatch for a validated packet: returns the type byte (0x0a..0x28)
 * or DJLINK_ERR_* if the framing is broken. */
int djlink_packet_type(const uint8_t *buf, size_t len);

/* Big-endian scalar helpers shared by the packet modules. */
uint16_t djlink_rd16(const uint8_t *p);
uint32_t djlink_rd32(const uint8_t *p);
void djlink_wr32(uint8_t *p, uint32_t v);

/* Copy a 20-byte padded name field out as a NUL-terminated C string.
 * dst must hold at least DJLINK_NAME_LEN + 1 bytes. */
void djlink_name_to_str(const uint8_t name[DJLINK_NAME_LEN], char *dst);

/* Pack a NUL-terminated C string into a 20-byte padded name field. */
void djlink_name_from_str(const char *src, uint8_t name[DJLINK_NAME_LEN]);

/* --- Pitch / BPM helpers -------------------------------------------------
 * Raw pitch format (beat + status packets): 0x00100000 = 0%,
 * 0x00000000 = -100%, 0x00200000 = +100%. */
float djlink_pitch_raw_to_percent(int32_t pitch_raw);
int32_t djlink_pitch_percent_to_raw(float percent);

/* Track BPM x100 (as carried in packets) adjusted by raw pitch -> effective
 * BPM to two decimals. Matches the byte equation in the DJ Link analysis. */
float djlink_effective_bpm(uint16_t bpm100, int32_t pitch_raw);

#ifdef __cplusplus
}
#endif
