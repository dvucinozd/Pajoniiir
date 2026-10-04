#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CDJ Status packet (port 50002, type 0x0a). Sent ~every 200 ms by players
 * that can see your keep-alives (virtual CDJ trick). Lengths vary by
 * generation: 0xd4 (nexus), 0xd0 (older), 0x11c/0x124 (nxs2/XDJ-1000),
 * 0x200 (CDJ-3000). Parse defensively via lenr; never require exact length.
 * See src/djlink_status.c for the header layout.
 */

typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t revision;      /* 0x20: 1 djm/rekordbox, 3 nxs, 4 xdj1000 */
    uint8_t device_number;
    uint16_t lenr;
    bool active;           /* 0x26 (u16): 1 = playing / searching / loading */
    uint8_t source_device; /* 0x28: 0 = none, else device track came from */
    uint8_t source_slot;   /* 0x29: DJLINK_SLOT_* */
    uint32_t rekordbox_id; /* 0x2c */
    uint32_t play_state;   /* 0x78 (u32 BE): DJLINK_PLAY_* */
    bool has_flag_bits;    /* 0x89 present (nexus and later) */
    uint8_t flags;         /* F bit field, see DJLINK_FLAG_* */
    int32_t pitch_raw;     /* physical pitch 0x8c, 0x00100000 = 0% */
    uint16_t bpm_state;    /* 0x90: 0x8000 rekordbox, 0x7fff unknown, 0 cd */
    uint16_t bpm100;       /* 0x92: 0xffff = no track */
    uint8_t master_meaningful; /* 0x9e: 0 no, 1 master w/ rekordbox, 2 nominal */
    uint8_t master_handoff; /* 0x9f (Mh): player the tempo master yields to,
                             * 0xff = none; build writes 0 as 0xff */
    uint32_t beat;         /* 0xa0: beat counter, 0xffffffff = unavailable */
    uint8_t beat_in_bar;   /* 0xa6 */
} djlink_status_t;

/* Source slot codes (byte 0x29). */
#define DJLINK_SLOT_NONE   0x00u
#define DJLINK_SLOT_CD     0x01u
#define DJLINK_SLOT_SD     0x02u
#define DJLINK_SLOT_USB    0x03u
#define DJLINK_SLOT_LAPTOP 0x04u

/* Play states (u32 at 0x78). */
#define DJLINK_PLAY_NO_TRACK       0x00u
#define DJLINK_PLAY_LOADING        0x02u
#define DJLINK_PLAY_PLAYING        0x03u
#define DJLINK_PLAY_LOOP           0x04u
#define DJLINK_PLAY_PAUSE          0x05u
#define DJLINK_PLAY_PAUSE_CUE      0x06u
#define DJLINK_PLAY_CUE_PLAY       0x07u
#define DJLINK_PLAY_CUE_SCRATCH    0x08u
#define DJLINK_PLAY_SEARCH         0x09u
#define DJLINK_PLAY_ENDED          0x11u
#define DJLINK_PLAY_EMERGENCY_LOOP 0x12u

/* Flag bits (byte F, 0x89): bit 1 BPM-sync-degraded, 3 on-air, 4 sync,
 * 5 tempo master, 6 playing. */
#define DJLINK_FLAG_BPM_MODE 0x02u
#define DJLINK_FLAG_ON_AIR   0x08u
#define DJLINK_FLAG_SYNC     0x10u
#define DJLINK_FLAG_MASTER   0x20u
#define DJLINK_FLAG_PLAYING  0x40u

/* Parse a CDJ (or mixer, revision 1, device number 0x21) status packet.
 * Only requires bytes up to the highest offset actually present; fields past
 * the end of a short packet are zeroed and has_flag_bits tells you what was
 * available. */
djlink_err_t djlink_status_parse(const uint8_t *buf, size_t len, djlink_status_t *out);

/* Build a nexus-generation CDJ status (0xd4 bytes, revision 3) from the
 * fields of `in` that a player fills: name (padded field, see
 * djlink_name_from_str), device_number, active, source_device / source_slot /
 * rekordbox_id, play_state, flags, pitch_raw, bpm100 (0xffff = no track),
 * master_meaningful, master_handoff, beat (0xffffffff = unknown), beat_in_bar. revision,
 * lenr, bpm_state and has_flag_bits are set here. `packet_counter` goes at
 * 0xc8. Returns the byte count or a negative DJLINK_ERR_*. */
#define DJLINK_STATUS_PACKET_LEN 0xd4u
int djlink_status_build(const djlink_status_t *in, uint32_t packet_counter,
                        uint8_t *out, size_t cap);

/* --- Keep-alive (virtual CDJ announcement) -------------------------------
 * Port 50000, type 0x06, 0x36 bytes, broadcast every ~2 s (mixers ~1.5 s).
 * Sending this is what makes real players/mixers start unicasting status to
 * your port 50002 socket. Use the real MAC/IP of your interface. Device
 * number: 1..4 (1..6 for CDJ-3000); higher numbers work but break dbserver
 * metadata queries.
 */

#define DJLINK_KEEPALIVE_PACKET_LEN 0x36u

typedef struct {
    const char *name;      /* device name, up to 20 bytes */
    uint8_t device_number; /* player number */
    uint8_t mac[6];
    uint32_t ip;        /* interface address, host order */
    uint8_t peer_count; /* devices seen on network, including self (0 -> 2) */
    uint8_t startup_flags; /* byte 0x25: 0x02 if we were first device on the
                            * network, 0x01 if we joined an occupied network */
} djlink_keepalive_t;

/* Builds the 0x36-byte CDJ-variant keep-alive; returns byte count or
 * negative DJLINK_ERR_*. */
int djlink_keepalive_build(const djlink_keepalive_t *in, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
