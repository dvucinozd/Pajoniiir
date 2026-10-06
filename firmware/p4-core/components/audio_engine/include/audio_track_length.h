/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v271: the length of the decoded file, not of the Rekordbox analysis.
 *
 * v270 hardware: 'duration 248000 ms (pdb) -> 248140 ms (pwv3 37221
 * entries)' and yet the decoder ran on to EOF ~7.5 s later. The PVBR table
 * holds ~12 kB per 620 ms entry at the measured ~19.3 kB/s, i.e. entries are
 * 248140/400 ms apart in real time: the analysis is correct but covers only
 * the first 248 s, and the file carries ~155 kB of audio past its last entry.
 * The PVBR table and the PWV3/PWAV waveforms therefore keep their own time
 * base (the analysis span); only the track length grows to the file length.
 * Rescaling them to the file length would shift every mid-track position.
 */

typedef enum {
    AUDIO_TRACK_LENGTH_NONE = 0,
    AUDIO_TRACK_LENGTH_XING,      /* Xing/Info or VBRI frame count */
    AUDIO_TRACK_LENGTH_PVBR,      /* PVBR byte rate carried to the end of the file */
    AUDIO_TRACK_LENGTH_BITRATE,   /* first-frame bitrate over the file size */
    AUDIO_TRACK_LENGTH_EOF,       /* measured at decoder EOF */
    AUDIO_TRACK_LENGTH_SCAN,      /* frame headers counted past the last PVBR entry (v272) */
    AUDIO_TRACK_LENGTH_DECODE,    /* decoder ran past the length, extended (v272) */
} audio_track_length_source_t;

/* Decoder output counted from a known file start, independently of seek/UI
 * position. A discontinuity or rate change withdraws measurement authority. */
typedef struct {
    uint64_t frames;
    uint32_t hz;
    bool anchored;
} audio_track_decode_length_t;

static inline void audio_track_decode_length_begin(audio_track_decode_length_t *m)
{
    *m = (audio_track_decode_length_t){.anchored = true};
}

static inline void audio_track_decode_length_invalidate(audio_track_decode_length_t *m)
{
    m->anchored = false;
}

static inline uint32_t audio_track_decode_length_ms(const audio_track_decode_length_t *m)
{
    if (!m->anchored || !m->hz ||
        m->frames > (uint64_t)UINT32_MAX * m->hz / 1000u) return 0u;
    return (uint32_t)(m->frames * 1000u / m->hz);
}

static inline void audio_track_decode_length_add(audio_track_decode_length_t *m,
                                                 uint32_t frames, uint32_t hz)
{
    if (!m->anchored || frames == 0u) return;
    if (!hz || (m->hz && hz != m->hz) || UINT64_MAX - m->frames < frames) {
        m->anchored = false;
        return;
    }
    m->hz = hz;
    m->frames += frames;
}

/*
 * v272 read "Xing 181080 ms, decoded end 185250 ms" (Nerdy Roller) as a lying
 * header. v273: it was not. Decoding the file gives exactly the Xing length;
 * the 4.2 s came from PVBR seeks landing early (the entries count from the
 * end of the ID3v2 tag, see audio_pvbr_geometry_t), which the decoded end
 * inherited. The Xing count is the length again; the scan and the PVBR
 * estimate are only compared with it for the log, and stand in when the file
 * has no header.
 */
typedef enum {
    AUDIO_XING_ABSENT = 0,
    AUDIO_XING_TRUSTED,           /* matches the scan / PVBR estimate */
    AUDIO_XING_MISMATCH,          /* does not, kept anyway (logged) */
    AUDIO_XING_UNVERIFIED,        /* nothing to check it against */
} audio_xing_verdict_t;

/* Xing agreement with the reference: 0.5 %, never under 200 ms. */
#define AUDIO_XING_TOLERANCE_PERMILLE 5u
#define AUDIO_XING_TOLERANCE_MIN_MS 200u
/* Tail scans past this many bytes fall back to the PVBR estimate. */
#define AUDIO_TAIL_SCAN_MAX_BYTES (1024u * 1024u)
/* Frame headers averaged for the bitrate estimate when there is no table. */
#define AUDIO_BITRATE_SCAN_FRAMES 64u
/* A decoder past the length without EOF extends it by this much each time. */
#define AUDIO_TRACK_EXTEND_STEP_MS 1000u

typedef size_t (*audio_track_read_fn)(void *ctx, size_t offset, void *dst, size_t bytes);

#define AUDIO_MP3_INDEX_SLOTS 256u
#define AUDIO_MP3_INDEX_MAX_BYTES (1024u * 1024u * 1024u)
#define AUDIO_MP3_INDEX_MAX_FRAMES 1000000u
typedef struct {
    uint32_t byte;
    uint32_t frame;
} audio_mp3_index_entry_t;

/* Fixed-memory, session-local index. Only a complete, constant-rate MPEG
 * header walk is authoritative; failed builds leave complete=false. */
typedef struct {
    audio_mp3_index_entry_t entries[AUDIO_MP3_INDEX_SLOTS];
    uint32_t count, stride, frames, hz, frame_samples, duration_ms;
    size_t file_size;
    size_t audio_end; /* excludes validated trailing tags/padding */
    bool complete;
} audio_mp3_index_t;

bool audio_mp3_index_build(audio_track_read_fn read, void *ctx, size_t file_size,
                           audio_mp3_index_t *out);
/* Finds an exact MPEG frame before the target with decoder reservoir lead.
 * At most stride header reads; the caller supplies cancellation/IO admission. */
bool audio_mp3_index_locate(const audio_mp3_index_t *index,
                            audio_track_read_fn read, void *ctx, uint32_t target_ms,
                            uint32_t *byte, uint32_t *skip_frames);

typedef struct {
    uint32_t frames;
    uint32_t hz;
    uint32_t ms;                  /* audio in the frames counted */
    size_t first;                 /* first frame start */
    size_t last_end;              /* end of the last whole frame */
    bool cbr;                     /* every frame the size of the first */
} audio_mp3_scan_t;

/* Counts Layer III frame headers in [start, end), up to max_frames (0 = all),
 * without decoding. Resyncs over junk and tags; a truncated last frame is not
 * counted. False when no frame was found. */
bool audio_mp3_scan(audio_track_read_fn read, void *ctx, size_t start, size_t end,
                    uint32_t max_frames, audio_mp3_scan_t *out);

/*
 * v273: where the Rekordbox PVBR entries really are. Checked against 1003
 * exported tables and their files (v272 hardware: California Dreaming played
 * its 10.5 s cue from 3.7 s):
 *  - the offsets count from the end of the ID3v2 tag, not from the start of
 *    the file. Read as file offsets they land one tag early: 133 kB (6.8 s)
 *    on California, 99.5 kB (4.2 s) on Nerdy Roller, 1.1 kB (24 ms, not
 *    heard) on Stubborn;
 *  - entry k is MPEG frame floor((k + 1) * N / 400) - 8, frame 0 being the
 *    Xing/Info frame and N the frame count including it (Xing count + 1). The
 *    entries therefore sit 120-260 ms after k * span / 400;
 *  - the Rekordbox time base (cues, beatgrid, waveforms) is the decoder
 *    output from the start of the file, the Xing frame decoding to one frame
 *    of silence: frame F starts at F * frame_samples / hz.
 * The upstream main-deck-p4 seek_pvbr has the same file-offset reading.
 */
typedef struct {
    size_t base;                  /* the entries count from here */
    uint32_t frames;              /* N, 0 = unknown: entries at k * span / len */
    uint32_t frame_samples;
    uint32_t hz;
} audio_pvbr_geometry_t;

#define AUDIO_PVBR_ENTRY_FRAME_LAG 8u
/* A seek lands at least this many frames before its target: a decoder
 * started mid-file outputs nothing for the first frame or three (the bit
 * reservoir), and those frames come out of the seek skip. */
#define AUDIO_PVBR_RESYNC_LEAD_FRAMES 4u

typedef struct {
    uint32_t hz;
    uint32_t frame_samples;
    uint32_t count;               /* Xing/Info or VBRI frame count, 0 = none */
} audio_mp3_first_frame_t;

/* The first MPEG audio frame in buf; false when there is none. */
bool audio_mp3_first_frame(const uint8_t *buf, size_t len, audio_mp3_first_frame_t *out);

/* N from the analysis span, for a file without a frame count. */
uint32_t audio_pvbr_frames_from_span(uint32_t span_ms, uint32_t hz, uint32_t frame_samples);

/* MPEG frame of entry idx (N = frames). */
uint32_t audio_pvbr_entry_frame(uint32_t idx, uint32_t frames, uint32_t len);

/* Track time of entry idx: exact with a frame count, k * span / len
 * without one. */
uint32_t audio_pvbr_entry_time_ms(const audio_pvbr_geometry_t *geom, uint32_t idx,
                                  uint32_t len, uint32_t span_ms);

/* Whether a frame (followed by another, or the end) starts at pos. */
bool audio_mp3_frame_at(audio_track_read_fn read, void *ctx, size_t pos, size_t end);

/* The base the entries count from: id3_size when frames sit there, else 0
 * when they sit at the raw offsets, else id3_size. */
size_t audio_pvbr_base(audio_track_read_fn read, void *ctx, const uint32_t *pvbr,
                       uint32_t len, size_t id3_size, size_t file_size);

/*
 * v302: the table Rekordbox would have written, for a file it never
 * analysed here (a DJ Link peer track: the peer serves no PVBR, vynull an
 * all-zero one). Without it every seek was seek_estimate's byte-linear guess,
 * which lands off on VBR files while the engine reports the target, so the
 * waveform and the beat grid ran apart from the audio after each cue, hot
 * cue, loop or beat jump. Walks the frame headers from the end of the ID3v2
 * tag, frame 0 being the Xing/Info frame, and stores the offset of frame
 * audio_pvbr_entry_frame(k, N, len) in entry k, N = Xing count + 1: the
 * engine then seeks it exactly like an exported table. False without a frame
 * count (CBR files without one seek right by estimate) or when the frames
 * run out before the last entry.
 */
bool audio_pvbr_build(audio_track_read_fn read, void *ctx, size_t file_size,
                      uint32_t *pvbr, uint32_t len);

typedef struct {
    uint32_t span_ms;             /* analysis */
    uint32_t xing_ms;             /* 0 = no header */
    uint32_t scan_ms;             /* last entry + tail scan, 0 = none */
    uint32_t pvbr_ms;             /* table extrapolation, 0 = none */
    uint32_t bitrate_ms;          /* averaged header bitrate, 0 = none */
    bool bitrate_cbr;             /* the averaged frames were constant: the
                                   * estimate can check a Xing header; a VBR
                                   * average of the first frames cannot */
} audio_track_length_inputs_t;

typedef struct {
    uint32_t file_ms;
    uint32_t track_ms;
    audio_track_length_source_t source;
    audio_xing_verdict_t xing;
} audio_track_length_decision_t;

audio_track_length_decision_t audio_track_length_decide(const audio_track_length_inputs_t *in);

/* Length after the decoder reached decoded_ms without EOF. */
uint32_t audio_track_length_extend(uint32_t track_ms, uint32_t decoded_ms);

/* Length at decoder EOF: the exact end after a runtime extension, otherwise
 * raised only past the tolerance. */
uint32_t audio_track_length_at_eof(uint32_t span_ms, uint32_t track_ms, uint32_t end_ms,
                                   bool extended);

const char *audio_xing_verdict_name(audio_xing_verdict_t verdict);

/* A file length within this of the analysis span keeps the span. */
#define AUDIO_TRACK_LENGTH_TOLERANCE_MS 1000u
/* Entries at the end of the table whose byte rate is carried past it. */
#define AUDIO_PVBR_TAIL_ENTRIES 40u
/* A seek past the table lands this far before its target and decodes the
 * rest, so an uneven tail bitrate costs skip frames, not a late landing. */
#define AUDIO_PVBR_TAIL_LEAD_MS 1000u

/* Bytes of the ID3v2 tag at the start of the file (0 if none). */
size_t audio_id3v2_size(const uint8_t *buf, size_t len);

/* Length from the Xing/Info or VBRI header of the first MPEG audio frame in
 * buf, 0 when that frame carries no frame count. */
uint32_t audio_mp3_header_duration_ms(const uint8_t *buf, size_t len);

/* Length implied by the PVBR table: its tail byte rate carried from the last
 * entry (at base + pvbr[last]) to the end of the file. 0 when the table is
 * unusable. */
uint32_t audio_pvbr_extrapolate_end_ms(const uint32_t *pvbr, uint32_t len,
                                       uint32_t span_ms, size_t base, size_t file_size);

/* Constant-bitrate estimate, 0 when unknown. */
uint32_t audio_bitrate_duration_ms(size_t audio_bytes, uint32_t bitrate_kbps);

/* Track length from the analysis span and the file length: the file length
 * when it runs past the span by more than the tolerance (or there is no
 * span), otherwise the span. */
uint32_t audio_track_length_resolve(uint32_t span_ms, uint32_t file_ms);

/* PVBR seek landing for target_ms on a track of track_ms. Inside the span:
 * the table entry at or before the target; with a frame count, the last
 * entry at least AUDIO_PVBR_RESYNC_LEAD_FRAMES before it, or the start of
 * the audio. Past it: interpolated between the last entry and the end of the
 * file (the tail byte rate when the track ends at the table),
 * AUDIO_PVBR_TAIL_LEAD_MS before the target. Returns the landing time and
 * stores its file byte in *byte. A NULL geom reads the entries as file
 * offsets at k * span / len (v263-v272). */
uint32_t audio_pvbr_locate(const uint32_t *pvbr, uint32_t len, uint32_t span_ms,
                           uint32_t track_ms, size_t file_size,
                           const audio_pvbr_geometry_t *geom,
                           uint32_t target_ms, uint32_t *byte);

const char *audio_track_length_source_name(audio_track_length_source_t source);

#ifdef __cplusplus
}
#endif
