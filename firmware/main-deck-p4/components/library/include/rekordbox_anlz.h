#pragma once
/*
 * rekordbox_anlz.h  —  Rekordbox ANLZ file parser
 *
 * Parses ANLZ0000.DAT + ANLZ0000.EXT from a Rekordbox-formatted USB drive.
 *
 * USB drive layout (HASH-BASED — NOT a path mirror of the audio file):
 *   PIONEER/USBANLZ/<P-hash>/<ID-hash>/ANLZ0000.DAT  — path, BPM, beatgrid, cues, waveform
 *   PIONEER/USBANLZ/<P-hash>/<ID-hash>/ANLZ0000.EXT  — high-res waveform (PWV3)
 *   PIONEER/USBANLZ/<P-hash>/<ID-hash>/ANLZ0000.2EX  — color waveform (newer Rekordbox)
 *
 * All multi-byte values in ANLZ files are big-endian.
 * Tags are located by walking the section headers (tag/header_size/segment_size);
 * a byte-scan fallback handles structurally broken files.
 *
 * Usage:
 *   anlz_metadata_t meta;
 *   esp_err_t rc = anlz_parse_dat("/usb/PIONEER/USBANLZ/P000/00000832/ANLZ0000.DAT", &meta);
 *   if (rc == ESP_OK) {
 *       anlz_parse_ext("/usb/PIONEER/USBANLZ/artist/track/ANLZ0000.EXT", &meta);  // optional
 *       // use meta.bpm, meta.beats, meta.cues, meta.waveform_low, meta.waveform_high ...
 *       anlz_free(&meta);
 *   }
 *
 * Compile-time option (PC test build only):
 *   #define ANLZ_STANDALONE_TEST   — replaces ESP_LOG with printf, removes esp_err.h dependency
 */

#ifdef ANLZ_STANDALONE_TEST
#  include <stdio.h>
#  define ESP_OK          0
#  define ESP_ERR_INVALID_ARG  1
#  define ESP_ERR_INVALID_SIZE 2
#  define ESP_ERR_NOT_FOUND    3
#  define ESP_ERR_NO_MEM       4
#  define ESP_FAIL             5
typedef int esp_err_t;
#  define ANLZ_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#  define ANLZ_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#  define ANLZ_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#  include "esp_err.h"
#  include "esp_log.h"
#  define ANLZ_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#  define ANLZ_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#  define ANLZ_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Tag IDs (4 ASCII bytes, stored as uint32_t big-endian) ───────────────── */
#define ANLZ_TAG_PMAI  0x504D4149u  /* 'PMAI' — file header            */
#define ANLZ_TAG_PPTH  0x50505448u  /* 'PPTH' — audio file path        */
#define ANLZ_TAG_PVBR  0x50564252u  /* 'PVBR' — VBR seek table         */
#define ANLZ_TAG_PQTZ  0x5051545Au  /* 'PQTZ' — beat grid              */
#define ANLZ_TAG_PWAV  0x50574156u  /* 'PWAV' — waveform low-res       */
#define ANLZ_TAG_PWV2  0x50575632u  /* 'PWV2' — waveform tiny          */
#define ANLZ_TAG_PCOB  0x50434F42u  /* 'PCOB' — cue objects container  */
#define ANLZ_TAG_PWV3  0x50575633u  /* 'PWV3' — waveform high-res      */

/* ── Sizes ────────────────────────────────────────────────────────────────── */
#define ANLZ_WAVEFORM_LOW_LEN    400u   /* PWAV: always 400 bytes          */
#define ANLZ_WAVEFORM_TINY_LEN   100u   /* PWV2: always 100 nibble entries */
#define ANLZ_VBR_TABLE_LEN       400u   /* PVBR: 400 × uint32_t offsets    */
#define ANLZ_MAX_CUES              8u   /* hot cues 0–7                    */
#define ANLZ_WAVEFORM_HIGH_MAX 131072u  /* PWV3: up to 128 KB (observed max ~62 KB) */
#define ANLZ_PATH_MAX            512u   /* audio path buffer               */

/* ── Beat grid entry (8 bytes, big-endian in file) ────────────────────────── */
typedef struct {
    uint16_t beat_phase;   /* phase within the bar (0–3 for beats 1–4)    */
    uint16_t bpm_x100;     /* BPM × 100  (e.g. 12850 → 128.50 BPM)       */
    uint32_t time_ms;      /* absolute position from start of track (ms)  */
} anlz_beat_t;

/* ── Cue type ─────────────────────────────────────────────────────────────── */
typedef enum {
    ANLZ_CUE_SINGLE = 1,   /* single hot cue point    */
    ANLZ_CUE_LOOP   = 2,   /* loop (start + end)      */
} anlz_cue_type_t;

/* ── Single cue / loop entry ─────────────────────────────────────────────── */
typedef struct {
    anlz_cue_type_t type;   /* single point or loop                        */
    uint8_t         index;  /* hot cue slot 0–7                            */
    uint32_t        start_ms;
    uint32_t        end_ms; /* loop end; 0 for single cues                 */
} anlz_cue_t;

/* ── Parsed metadata for one track ──────────────────────────────────────────
 *
 * Heap allocations:
 *   beats         — heap-allocated array of beat_count entries, or NULL
 *   waveform_high — heap-allocated array of waveform_high_len bytes, or NULL
 *
 * All other fields are inline.  Call anlz_free() when done.
 */
typedef struct anlz_metadata {
    /* Path to audio file (UTF-8, filtered from UTF-16-BE PPTH) */
    char audio_path[ANLZ_PATH_MAX];

    /* Beat grid (from PQTZ) */
    anlz_beat_t *beats;      /* heap, beat_count entries; NULL if absent   */
    uint16_t     beat_count; /* number of beat entries                     */
    uint16_t     bpm;        /* BPM rounded from first entry's bpm_x100    */

    /* Hot cues / loops (from PCOB/PCPT) */
    anlz_cue_t cues[ANLZ_MAX_CUES];
    uint8_t    cue_count;

    /* VBR seek table (from PVBR) — 400 file-byte offsets */
    uint32_t vbr[ANLZ_VBR_TABLE_LEN];
    bool     has_vbr;

    /* Low-resolution waveform (from PWAV) — 400 bytes */
    uint8_t waveform_low[ANLZ_WAVEFORM_LOW_LEN];
    bool    has_waveform_low;

    /* High-resolution waveform (from PWV3 in .EXT) — heap */
    uint8_t  *waveform_high;     /* heap; NULL until anlz_parse_ext() called */
    uint32_t  waveform_high_len; /* number of valid bytes in waveform_high   */
} anlz_metadata_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Parse ANLZ0000.DAT into *out.
 *
 * Reads PPTH, PVBR, PQTZ, PWAV, PCOB tags.
 * beats is heap-allocated; call anlz_free() when done.
 *
 * @param dat_path  Absolute path to ANLZ0000.DAT on the mounted USB drive.
 * @param out       Caller-allocated struct; zeroed on entry by this function.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG / ESP_ERR_NOT_FOUND on failure.
 */
esp_err_t anlz_parse_dat(const char *dat_path, anlz_metadata_t *out);

/**
 * Parse ANLZ0000.EXT and populate the high-res waveform field.
 *
 * Must be called after anlz_parse_dat().  Reads the PWV3 tag.
 * waveform_high is heap-allocated; anlz_free() will release it.
 *
 * @param ext_path  Absolute path to ANLZ0000.EXT on the mounted USB drive.
 * @param meta      Already-parsed metadata struct from anlz_parse_dat().
 * @return ESP_OK on success.
 */
esp_err_t anlz_parse_ext(const char *ext_path, anlz_metadata_t *meta);

/** Deep-copy metadata, including heap-owned beats and high-resolution
 * waveform data. `out` must not already own allocations. */
esp_err_t anlz_clone(const anlz_metadata_t *src, anlz_metadata_t *out);

/**
 * Free all heap-allocated fields inside meta (beats, waveform_high).
 * Does NOT free the struct itself (caller-allocated).
 * Safe to call multiple times (idempotent).
 */
void anlz_free(anlz_metadata_t *meta);

#ifdef __cplusplus
}
#endif
