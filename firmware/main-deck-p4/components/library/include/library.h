#pragma once

// Media library — USB drive (Rekordbox format) browser.
//
// Track index is built from export.pdb (Pioneer Hardware Database) at startup.
// The PDB gives us, for every track:
//   • file_path  — audio file path on USB (/Contents/...)
//   • anlz_path  — direct path to ANLZ0000.DAT (/PIONEER/USBANLZ/.../ANLZ0000.DAT)
//   • title, artist, album
//   • bpm (from PDB; overwritten with more precise value from ANLZ beat-grid)
//   • total audio duration (retained when ANLZ metadata is applied)
//
// ANLZ metadata (precise BPM, beat-grid, cues, waveform) is loaded on-demand
// via library_load_anlz(), which reads ANLZ0000.DAT using the stored anlz_path.
//
// USB drive folder structure:
//   PIONEER/rekordbox/export.pdb               — track index (Pioneer HW DB)
//   PIONEER/USBANLZ/<hash>/<hash>/ANLZ0000.DAT — beat-grid, cues, waveform
//   PIONEER/USBANLZ/<hash>/<hash>/ANLZ0000.EXT — high-res waveform (PWV3)
//
// See docs/rekordbox-format-analysis.md for full format spec.

#include <stdint.h>
#include "esp_err.h"
#include "rekordbox_anlz.h"

#define LIBRARY_PATH_MAX  256
#define LIBRARY_STR_MAX   128

typedef struct {
    /* Populated by library_init() from export.pdb */
    char     path[LIBRARY_PATH_MAX];
    char     anlz_path[LIBRARY_PATH_MAX];
    char     title[LIBRARY_STR_MAX];
    char     artist[LIBRARY_STR_MAX];
    char     album[LIBRARY_STR_MAX];
    char     key[16];
    uint32_t track_id;
    uint16_t bpm;
    uint32_t duration_ms;

    /* Populated by library_load_anlz() */
    uint8_t  waveform_low[400];
    uint8_t  has_waveform;
    uint8_t  has_anlz;
    uint32_t pvbr[400];
    uint8_t  has_pvbr;
} library_track_t;

/* library_init() transactionally publishes an immutable track store. Logical
 * row order is held separately, so library_sort() copies only compact uint16_t
 * handles and never moves the published library_track_t records. */
esp_err_t library_init(void);
void      library_clear(void);
uint32_t  library_generation(void);
int       library_count(void);
esp_err_t library_get(int index, library_track_t *out);
esp_err_t library_get_summary(int index,
                              uint16_t *out_bpm,
                              uint32_t *out_duration_ms);
/* Identity of one logical row without copying the ~2.9 KB track record. Use this
 * (and library_find_row_by_key) for highlight/selection/lookup work; library_get()
 * is for callers that genuinely need the whole record. */
esp_err_t library_get_row_key(int index, uint32_t *out_key);
/* Logical row currently holding `track_key`, or -1. Single pass under one lock
 * instead of N locked full-record copies. */
int       library_find_row_by_key(uint32_t track_key);
#ifdef WIN32
/* Simulator only: pointer into the immutable store for the current logical row.
 * Sorting keeps it valid; a later library_init() rebuild may replace the store. */
library_track_t *library_get_ptr(int index);
#endif
uint32_t library_track_key(const library_track_t *track);

/* Resolve and publish one authoritative ANLZ object. Existing nonzero PDB/audio
 * duration is retained; the final beat is only a fallback when duration is zero.
 * A failed resolve retires stale current metadata. */
esp_err_t library_load_anlz(library_track_t *track);
void      library_sort(int field_type, bool descending);

void library_last_anlz_load_stats(uint32_t *out_elapsed_ms, uint8_t *out_source,
                                  bool *out_cache_written);
esp_err_t library_clone_current_anlz(anlz_metadata_t *out);
void library_free_current_anlz(void);

/* Selected-row state used by the UI highlight/simulator bridge. This is not a
 * deck-load API; real loads use media_catalog identity + generation. */
void library_set_selected_track_index(int track_index);
int  library_selected_track_index(void);

