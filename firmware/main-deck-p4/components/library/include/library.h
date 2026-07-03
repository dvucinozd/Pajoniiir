#pragma once

// Media library — USB drive (Rekordbox format) browser.
//
// Track index is built from export.pdb (Pioneer Hardware Database) at startup.
// The PDB gives us, for every track:
//   • file_path  — audio file path on USB (/Contents/...)
//   • anlz_path  — direct path to ANLZ0000.DAT (/PIONEER/USBANLZ/.../ANLZ0000.DAT)
//   • title, artist, album
//   • bpm (from PDB; overwritten with more precise value from ANLZ beat-grid)
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
    char     path[LIBRARY_PATH_MAX];      // audio file path on USB: /Contents/...
    char     anlz_path[LIBRARY_PATH_MAX]; // ANLZ file: /PIONEER/USBANLZ/.../ANLZ0000.DAT
    char     title[LIBRARY_STR_MAX];      // track title (or filename if no ID3 title)
    char     artist[LIBRARY_STR_MAX];     // artist name (empty string if unknown)
    char     album[LIBRARY_STR_MAX];      // album name  (empty string if unknown)
    char     key[16];                     // Camelot key (e.g. 8A, 9A)
    uint32_t track_id;                    // Rekordbox internal ID
    uint16_t bpm;                         // BPM from PDB (overwritten by ANLZ if loaded)

    /* Populated by library_load_anlz() */
    uint32_t duration_ms;                 // total duration (from last beat-grid entry)
    uint8_t  waveform_low[400];           // PWAV 400-byte low-res waveform (0 if not loaded)
    uint8_t  has_waveform;                // 1 if waveform_low is valid
    uint8_t  has_anlz;                    // 1 if ANLZ metadata was loaded
    uint32_t pvbr[400];                   // PVBR VBR seek table: 400 file-byte offsets
    uint8_t  has_pvbr;                    // 1 if pvbr[] is valid
} library_track_t;

esp_err_t library_init(void);                                // mount USB, open PDB, build index
void      library_clear(void);                               // clear active index after USB removal
uint32_t  library_generation(void);                          // increments on init/clear/sort
int       library_count(void);                               // number of tracks found
esp_err_t library_get(int index, library_track_t *out);      // get track by index
esp_err_t library_get_summary(int index,                     // copy bpm/duration under the lock
                              uint16_t *out_bpm,
                              uint32_t *out_duration_ms);
#ifdef WIN32
// Simulator only: direct pointer into the live index. Reloads and sorts
// republish the index, so firmware must use the copying accessors above.
library_track_t *library_get_ptr(int index);
#endif
uint32_t  library_track_key(const library_track_t *track);   // stable ID for UI/cache use
esp_err_t library_load_anlz(library_track_t *track);         // populate precise BPM/cues from ANLZ
void      library_sort(int field_type, bool descending);     // Sort track list (0=Artist, 1=Title, 2=BPM)

/* Detailed metadata of the currently loaded track (beats, cues, high-res waveform) */
esp_err_t library_load_current_anlz(const library_track_t *track);
const anlz_metadata_t *library_get_current_anlz(void);
void library_free_current_anlz(void);

/* UI track selection helpers — track which track index is loaded. */
void mock_library_load_track_to_deck(int track_index);
int  mock_library_get_current_track_index(void);
