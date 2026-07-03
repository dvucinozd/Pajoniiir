#pragma once
/*
 * rekordbox_pdb.h  —  Pioneer Hardware Database (PDB) parser
 *
 * Reads export.pdb from a Rekordbox-formatted USB drive.
 * Provides per-track metadata: title, artist, album, file path, ANLZ path, BPM.
 *
 * PDB file location on USB:  PIONEER/rekordbox/export.pdb
 *
 * Format references:
 *   https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/
 *   https://github.com/Deep-Symmetry/crate-digger
 *
 * All multi-byte fields in PDB files are little-endian.
 *
 * Usage:
 *   pdb_t *pdb;
 *   esp_err_t rc = pdb_open("/usb/PIONEER/rekordbox/export.pdb", &pdb);
 *   if (rc == ESP_OK) {
 *       for (int i = 0; i < pdb_track_count(pdb); i++) {
 *           pdb_track_t t;
 *           pdb_get_track(pdb, i, &t);
 *           // t.title, t.artist, t.file_path, t.anlz_path, t.bpm ...
 *       }
 *       pdb_close(pdb);
 *   }
 *
 * Compile-time option (PC test build only):
 *   -DREKORDBOX_PDB_STANDALONE_TEST  — replaces ESP_LOG/esp_err.h with stdio/int
 */

#ifdef REKORDBOX_PDB_STANDALONE_TEST
#  include <stdio.h>
#  define ESP_OK               0
#  define ESP_ERR_INVALID_ARG  1
#  define ESP_ERR_NOT_FOUND    2
#  define ESP_ERR_NO_MEM       3
#  define ESP_FAIL             4
typedef int esp_err_t;
#  define PDB_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#  define PDB_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#  define PDB_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#  include "esp_err.h"
#  include "esp_log.h"
#  define PDB_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#  define PDB_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#  define PDB_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── String / path buffer sizes ──────────────────────────────────────────── */
#define PDB_STR_MAX       128u   /* title, artist, album                      */
#define PDB_PATH_MAX      256u   /* file_path, anlz_path                      */
#define PDB_STR_NAME_MAX   96u   /* internal: name-table entry buffer         */

/* ── Parsed track descriptor ─────────────────────────────────────────────── */
typedef struct {
    uint32_t track_id;                  /* Rekordbox internal track ID            */
    uint16_t bpm;                       /* BPM, rounded (bpm_x100 / 100)         */
    uint16_t duration_s;                /* Duration in seconds                    */
    char     key[8];                    /* Musical key name from the Keys table   */
                                        /* (e.g. "Am", "8A"); empty if unknown    */
    char     title[PDB_STR_MAX];        /* Title (or filename when title absent)  */
    char     artist[PDB_STR_MAX];       /* Artist name (empty string if unknown)  */
    char     album[PDB_STR_MAX];        /* Album name  (empty string if unknown)  */
    char     file_path[PDB_PATH_MAX];   /* Audio file path on USB: /Contents/...  */
    char     anlz_path[PDB_PATH_MAX];   /* ANLZ path on USB:                      */
                                        /*   /PIONEER/USBANLZ/<P>/<ID>/ANLZ0000.DAT */
} pdb_track_t;

/* ── Opaque PDB handle ───────────────────────────────────────────────────── */
typedef struct pdb_s pdb_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * Open and parse export.pdb.
 *
 * Reads the entire file, then parses the Tracks, Artists, and Albums tables.
 * Allocates pdb_t on heap; call pdb_close() when done.
 *
 * @param pdb_path  Absolute path to export.pdb.
 * @param out       Receives pointer to pdb_t on success.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND / ESP_ERR_NO_MEM / ESP_FAIL otherwise.
 */
esp_err_t pdb_open(const char *pdb_path, pdb_t **out);

/**
 * Close and free a pdb_t handle.  Safe to call with NULL.
 */
void pdb_close(pdb_t *pdb);

/**
 * Return the number of parsed tracks.
 */
int pdb_track_count(const pdb_t *pdb);

/**
 * Get a track by index (0-based).
 *
 * @param pdb    Handle from pdb_open().
 * @param index  0 ≤ index < pdb_track_count(pdb).
 * @param out    Filled with a copy of the track data.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if index out of range.
 */
esp_err_t pdb_get_track(const pdb_t *pdb, int index, pdb_track_t *out);

#ifdef REKORDBOX_PDB_STANDALONE_TEST
esp_err_t pdb_test_decode_devicesql_string(const uint8_t *data, size_t data_len,
                                           char *dst, size_t dst_sz);
#endif

#ifdef __cplusplus
}
#endif
