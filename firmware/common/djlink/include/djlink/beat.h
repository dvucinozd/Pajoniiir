#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Beat packet (port 50001, type 0x28, fixed 0x60 bytes). Broadcast by
 * playing CDJs with rekordbox-analyzed tracks (and continuously by mixers as
 * a backup metronome) on every beat.
 *
 * Layout (offsets per the DJ Link analysis, cross-checked with
 * python-prodj-link):
 *   0x00 magic, 0x0a type=0x28, 0x0b..0x1e device name,
 *   0x1f..0x20 u1 (0x0100 CDJ, 0x0101 rekordbox),
 *   0x21 device number, 0x22..0x23 = 00 3c (subtype/lenr),
 *   0x24 next beat ms, 0x28 second beat ms, 0x2c next bar ms,
 *   0x30 fourth beat ms, 0x34 second bar ms, 0x38 eighth beat ms
 *   (0xffffffff = beat/bar beyond end of track),
 *   0x3c..0x53 zero padding,
 *   0x54..0x57 pitch (raw, BE), 0x58..0x59 zero (0xff while scratching),
 *   0x5a..0x5b BPM x100 (BE), 0x5c beat-in-bar (1..4),
 *   0x5d..0x5e zero, 0x5f device number (redundant copy).
 * All timing values assume 0% pitch; scale by pitch yourself.
 */

#define DJLINK_BEAT_PACKET_LEN 0x60u

typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t device_number;
    uint32_t next_beat_ms;
    uint32_t second_beat_ms;
    uint32_t next_bar_ms;
    uint32_t fourth_beat_ms;
    uint32_t second_bar_ms;
    uint32_t eighth_beat_ms;
    int32_t pitch_raw;   /* 0x00100000 = 0% */
    uint16_t bpm100;     /* track BPM * 100 */
    uint8_t beat_in_bar; /* 1..4 (0 from pre-nexus or unanalyzed source) */
} djlink_beat_t;

djlink_err_t djlink_beat_parse(const uint8_t *buf, size_t len, djlink_beat_t *out);

/* Builds a full 0x60-byte beat packet; returns byte count or negative
 * DJLINK_ERR_*. */
int djlink_beat_build(const djlink_beat_t *in, uint8_t *out, size_t cap);

/* --- Absolute Position packet (CDJ-3000+) --------------------------------
 * Port 50001, type 0x0b, fixed 0x3c bytes, sent every 30 ms while a track is
 * loaded (even paused). Reliable playhead even while scratching / looping.
 *
 *   0x00 magic, 0x0a type=0x0b, 0x0b..0x1e device name,
 *   0x1f..0x20 u1, 0x21 device number, 0x22..0x23 = 00 09 (subtype),
 *   0x24 track length (seconds), 0x28 playhead (ms),
 *   0x2c pitch percent x100 (e.g. 3.26% -> 326),
 *   0x30..0x37 zero,
 *   0x38..0x3b BPM x10 (BE), 0xffffffff = unknown.
 */

#define DJLINK_POSITION_PACKET_LEN 0x3cu

typedef struct {
    uint8_t name[DJLINK_NAME_LEN];
    uint8_t device_number;
    uint32_t track_length_s;
    uint32_t playhead_ms;
    int32_t pitch_x100; /* pitch slider percent * 100 */
    int32_t bpm10;      /* effective BPM * 10, -1 = unknown */
} djlink_position_t;

djlink_err_t djlink_position_parse(const uint8_t *buf, size_t len, djlink_position_t *out);

/* Builds a full 0x3c-byte absolute-position packet; returns byte count or
 * negative DJLINK_ERR_*. */
int djlink_position_build(const djlink_position_t *in, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
