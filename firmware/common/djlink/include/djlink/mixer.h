#pragma once

#include "djlink/packet.h"
#include "djlink/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mixer-integration and load-control packets.
 * Port 50001 family (fader / on-air) uses the same header as beat packets
 * (name at 0x0b, 0x01 at 0x1f, 0x00 at 0x20, device number 0x21, lenr at
 * 0x22..0x23 BE). Port 50002 family (load track) uses the status header.
 * Layouts per mixer_integration.html / loading_tracks.html, cross-checked
 * with python-prodj-link.
 */

/* --- Fader start (port 50001, type 0x02, 0x28 bytes) --------------------- */
#define DJLINK_FADER_PACKET_LEN 0x28u

typedef enum {
    DJLINK_FADER_START = 0x00,
    DJLINK_FADER_STOP = 0x01,
    DJLINK_FADER_IGNORE = 0x02,
} djlink_fader_cmd_t;

typedef struct {
    uint8_t name[DJLINK_NAME_LEN]; /* mixer device name */
    uint8_t mixer_number;    /* mixer device number (usually 0x21) */
    uint8_t player_number;   /* sender number in header (0x21) */
    djlink_fader_cmd_t players[4]; /* command per player 1..4 (0x24..0x27) */
} djlink_fader_t;

int djlink_fader_build(const djlink_fader_t *in, uint8_t *out, size_t cap);
djlink_err_t djlink_fader_parse(const uint8_t *buf, size_t len, djlink_fader_t *out);

/* --- Channels on-air (port 50001, type 0x03, 0x28 bytes) ----------------- */
#define DJLINK_ONAIR_PACKET_LEN 0x28u

typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t mixer_number;
    uint8_t on_air[4]; /* per channel: non-zero = channel is on air */
} djlink_onair_t;

int djlink_onair_build(const djlink_onair_t *in, uint8_t *out, size_t cap);
djlink_err_t djlink_onair_parse(const uint8_t *buf, size_t len, djlink_onair_t *out);

/* --- Mixer status (port 50002, type 0x29, 0x38 bytes) --------------------
 * Header like CDJ status (name at 0x0b, revision 1, device number 0x21,
 * lenr 0x14). Content: state u16 BE at 0x26 (F bits: 0xf0 master, 0xd0
 * not), physical pitch 0x28..0x2b (always 0x00100000), BPM x100 u16 BE at
 * 0x2e, beat-in-bar at 0x37. Master-handoff byte at 0x36 (Mh).
 */
#define DJLINK_MIXER_STATUS_PACKET_LEN 0x38u

typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t device_number;
    uint16_t lenr;
    uint8_t flags;    /* 0x27: DJLINK_FLAG_* (0xf0 master, 0xd0 not) */
    int32_t pitch_raw; /* 0x28: always +0% */
    uint16_t bpm100;  /* 0x2e: master BPM, invalid for unanalyzed sources */
    uint8_t master_handoff; /* 0x36: 0x00 none, 0xff normal, else new master */
    uint8_t beat_in_bar;    /* 0x37 */
} djlink_mixer_status_t;

djlink_err_t djlink_mixer_status_parse(const uint8_t *buf, size_t len,
                                       djlink_mixer_status_t *out);

/* --- Load track (port 50002, type 0x19) -----------------------------------
 * Header: name 0x0b..0x1e, 0x01 at 0x1f, revision 4 at 0x20, device number
 * 0x21, lenr at 0x22, number copy 0x24, flag 0x25. Content: load target
 * player 0x28, slot 0x29, 0x0100 at 0x2a, rekordbox track ID at 0x2c,
 * 0x32 at 0x30, zero tail. Total 0x58.
 */
#define DJLINK_LOAD_TRACK_PACKET_LEN 0x58u

typedef struct {
    const char *name;       /* sender (commander) device name */
    uint8_t sender_number;  /* header device number (0x21) */
    uint8_t target_player;  /* 0x28: player to load the track on */
    uint8_t slot;           /* 0x29: DJLINK_SLOT_* */
    uint32_t rekordbox_id;  /* 0x2c */
} djlink_load_track_t;

int djlink_load_track_build(const djlink_load_track_t *in, uint8_t *out, size_t cap);
djlink_err_t djlink_load_track_parse(const uint8_t *buf, size_t len, djlink_load_track_t *out);

/* Load-track acknowledgment (type 0x1a): same header, empty content
 * (padding 2). Reports sender device number and lenr only. */
#define DJLINK_LOAD_ACK_PACKET_LEN 0x26u

typedef struct {
    uint8_t device_number;
} djlink_load_ack_t;

int djlink_load_ack_build(uint8_t device_number, const char *name, uint8_t *out, size_t cap);
djlink_err_t djlink_load_ack_parse(const uint8_t *buf, size_t len, djlink_load_ack_t *out);

#ifdef __cplusplus
}
#endif
