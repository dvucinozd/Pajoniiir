#pragma once
//
// dj_link_anlz — pure (host-testable) conversion of a peer's dbserver
// analysis replies into rekordbox ANLZ files, v300.
//
// A track downloaded from a peer (v249 NFS, v297 rekordbox collection) has
// no ANLZ0000.DAT/.EXT next to it: a collection source (rekordbox, vynull)
// does not even have one to fetch. Its dbserver does serve the analysis:
//   wave detail 0x2904 -> 0x4a02, blob = the PWV3 body (1 byte per 1/150 s,
//                          bits 4:0 height, bits 7:5 whiteness)
//   beat grid   0x2204 -> 0x4602, blob = 20-byte preamble, then 16 bytes per
//                          beat, little-endian: u16 beat in bar (1..4),
//                          u16 BPM x100, u32 time ms, 8 bytes padding
//   cues (v303) 0x2b04 -> 0x4e02, blob = the cue entries back to back,
//                          little-endian: u32 entry length (124, or 76 for
//                          a CDJ-saved legacy cue), u16 number at 0x04 (1..8
//                          = hot cue A..H, anything else a memory cue), u16
//                          type at 0x06 (1 cue, 2 loop), u32 time ms at
//                          0x0c, u32 loop end ms at 0x20 (0xffffffff if none)
// (beat-link WaveformDetail / BeatGrid / CueList, vynull dbserver/track.go
// and cuepoints.go). The fetch job writes them as <key>.DAT (PMAI, PPTH,
// PQTZ, PWAV, PCOB hot cues + PCOB memory cues) and <key>.EXT (PMAI, PWV3)
// beside the cached audio, so the deck parses a peer track with the same
// anlz_parse_dat / anlz_parse_ext as a local USB track.
//
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DJ_LINK_ANLZ_PREVIEW_LEN    400u    /* PWAV columns */
#define DJ_LINK_ANLZ_WAVE_MAX       131072u /* = ANLZ_WAVEFORM_HIGH_MAX */
/* v314: PWV4 colour preview: 6-byte entries, at most 1200 (= the library's
 * ANLZ_COLOR_PREVIEW_ENTRY / _MAX); the 0x2c04 blob adds the LE u32 length
 * and the 24-byte section head. */
#define DJ_LINK_ANLZ_COLOR_ENTRY    6u
#define DJ_LINK_ANLZ_COLOR_MAX      7200u
#define DJ_LINK_ANLZ_COLOR_BLOB_MAX (4u + 24u + DJ_LINK_ANLZ_COLOR_MAX)
#define DJ_LINK_ANLZ_GRID_HEAD      20u
#define DJ_LINK_ANLZ_GRID_ENTRY     16u
#define DJ_LINK_ANLZ_GRID_MAX_BEATS 4096u   /* 68 min at 60 BPM */
#define DJ_LINK_ANLZ_GRID_MAX \
    (DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * DJ_LINK_ANLZ_GRID_MAX_BEATS)

typedef struct {
    uint16_t beat_in_bar;   /* 1..4, as the peer and PQTZ store it */
    uint16_t bpm100;
    uint32_t time_ms;
} dj_link_anlz_beat_t;

#define DJ_LINK_ANLZ_CUE_ENTRY_MIN  0x24u   /* through the loop end */
#define DJ_LINK_ANLZ_CUES_MAX_ENTRIES 64u

typedef struct {
    uint8_t  hot_cue;       /* 1..8 = A..H, 0 = memory cue */
    bool     loop;
    uint32_t time_ms;
    uint32_t loop_end_ms;   /* loops only */
} dj_link_anlz_cue_t;

/* Entries in a cue-list blob (stops at the first entry that does not fit,
 * at most DJ_LINK_ANLZ_CUES_MAX_ENTRIES). */
size_t dj_link_anlz_cue_count(const uint8_t *blob, size_t len);
/* Entry i of the blob; false past the end. */
bool dj_link_anlz_cue(const uint8_t *blob, size_t len, size_t i, dj_link_anlz_cue_t *out);

/* v303: what the fetch job keeps once the peer has answered (or the wait
 * ran out). The analysis is asked on every load, cache hit or not, so an
 * edit at the source (beat grid, cues, re-analysis) reaches the deck; the
 * cached DAT is only replaced by a complete answer, so a slow or partial
 * reply never trades a good analysis for a worse one. answered = the peer
 * replied with the expected type (len may be 0). */
typedef struct {
    bool   dat_cached;      /* <key>.DAT exists */
    bool   wave_answered;
    bool   grid_answered;
    bool   cues_answered;
    size_t wave_len;        /* PWV3 bytes */
    size_t beats;           /* dj_link_anlz_grid_count */
    size_t cues;            /* dj_link_anlz_cue_count */
} dj_link_anlz_answers_t;

typedef struct {
    bool write_dat;
    bool cue_lists;         /* the DAT carries PCOB lists (even empty) */
    bool write_ext;
    bool drop_ext;          /* a stale <key>.EXT goes: the new DAT has no wave */
} dj_link_anlz_commit_t;

dj_link_anlz_commit_t dj_link_anlz_commit(const dj_link_anlz_answers_t *a);

/* Beats in a dbserver beat-grid blob (0 if too short to hold any). */
size_t dj_link_anlz_grid_count(const uint8_t *blob, size_t len);
/* Beat i of the blob; false past the end. */
bool dj_link_anlz_grid_beat(const uint8_t *blob, size_t len, size_t i,
                            dj_link_anlz_beat_t *out);

/* PWAV-style 400-column overview from the PWV3 detail: each column keeps
 * the tallest entry of its span (height and whiteness). False if detail is
 * empty (out zeroed). */
bool dj_link_anlz_preview(const uint8_t *detail, size_t len,
                          uint8_t out[DJ_LINK_ANLZ_PREVIEW_LEN]);

/* Byte sink: true when all len bytes were written. */
typedef bool (*dj_link_anlz_write_fn)(void *ctx, const void *data, size_t len);

/* <key>.DAT: PPTH = audio_path (ASCII expected; others become '?'), PQTZ
 * from the grid blob (omitted without beats), PWAV from the detail
 * (omitted without it). v303: with cues non-NULL, two PCOB sections from
 * the cue-list blob, hot cues (list 1) then memory cues (list 0), written
 * even when empty: they tell the deck the source has no cues (so seeded
 * cues it dropped go away too). cues NULL = the cues are unknown, no PCOB. */
bool dj_link_anlz_write_dat(dj_link_anlz_write_fn write, void *ctx, const char *audio_path,
                            const uint8_t *grid, size_t grid_len,
                            const uint8_t *detail, size_t detail_len,
                            const uint8_t *cues, size_t cues_len);
/* <key>.EXT: PWV3 = the detail (at most DJ_LINK_ANLZ_WAVE_MAX bytes);
 * v314, then PWV4 = the colour entries when color is non-NULL (from
 * dj_link_anlz_color_entries; head: entry size, count, 0 as rekordbox).
 * False without detail. */
bool dj_link_anlz_write_ext(dj_link_anlz_write_fn write, void *ctx,
                            const uint8_t *detail, size_t detail_len,
                            const uint8_t *color, size_t color_len);

/* v314: the PWV4 entries of a 0x2c04 reply blob (LE u32 length, then the
 * "PWV4" section): *entries points into blob, *len is a whole number of
 * DJ_LINK_ANLZ_COLOR_ENTRY entries (at most DJ_LINK_ANLZ_COLOR_MAX). False
 * for anything else (another tag or entry size, truncated, empty). */
bool dj_link_anlz_color_entries(const uint8_t *blob, size_t blob_len,
                                const uint8_t **entries, size_t *len);

#ifdef __cplusplus
}
#endif
