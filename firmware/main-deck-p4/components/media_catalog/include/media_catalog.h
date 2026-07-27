#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rekordbox_anlz.h"

typedef struct {
    uint32_t track_key;
    uint32_t rekordbox_track_id;
    uint16_t bpm;
    uint32_t duration_ms;
    char title[96];
    char artist[64];
    char album[64];
} media_catalog_track_t;

typedef struct {
    uint32_t track_key;
    uint16_t bpm;
    uint32_t duration_ms;
    char title[96];
    char artist[64];
    char key[16];
} media_catalog_row_t;

typedef struct {
    uint32_t track_key;
    char audio_path[272];
    char dat_path[272];
    char ext_path[272];
    uint32_t duration_ms;
    uint16_t bpm;
    uint8_t waveform_low[400];
    uint8_t has_waveform;
    uint32_t pvbr[400];
    uint8_t has_pvbr;
} media_loaded_track_t;

esp_err_t media_catalog_init(void);
int media_catalog_count(void);
uint32_t media_catalog_generation(void);
bool media_catalog_load_in_progress(void);
esp_err_t media_catalog_get(int index, media_catalog_track_t *out_track);
esp_err_t media_catalog_get_row(int index, media_catalog_row_t *out_row);
void media_catalog_sort(int field_type, bool descending);

/* Legacy index entry point retained for local UI compatibility. It snapshots the
 * stable identity and generation, then delegates to the transactional loader. */
esp_err_t media_catalog_load(int index, media_loaded_track_t *out_loaded);

/* Resolve and load a track by stable identity. Sorting is serialized against the
 * operation. ESP_ERR_INVALID_STATE means the supplied generation is stale or the
 * USB catalog changed before completion; callers must not reset or load a deck in
 * that case. */
esp_err_t media_catalog_load_by_identity(uint32_t track_key,
                                         uint32_t expected_generation,
                                         media_catalog_track_t *out_track,
                                         media_loaded_track_t *out_loaded);

/* Return an owned deep copy. Caller calls anlz_free(). */
esp_err_t media_catalog_clone_loaded_anlz(anlz_metadata_t *out);
