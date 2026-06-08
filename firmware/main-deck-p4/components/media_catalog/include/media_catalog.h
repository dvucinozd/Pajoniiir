#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cdj_link_protocol.h"
#include "esp_err.h"
#include "rekordbox_anlz.h"

typedef cdj_link_track_record_t media_catalog_track_t;

typedef struct {
    uint32_t track_key;
    uint16_t bpm;
    uint32_t duration_ms;
    char title[96];
    char artist[64];
} media_catalog_row_t;

esp_err_t media_catalog_init(void);
void media_catalog_set_source(media_source_t source);
media_source_t media_catalog_get_source(void);
esp_err_t media_catalog_refresh_remote(void);
int media_catalog_count(void);
esp_err_t media_catalog_get(int index, media_catalog_track_t *out_track);
esp_err_t media_catalog_get_row(int index, media_catalog_row_t *out_row);
void media_catalog_sort(int field_type, bool descending);
esp_err_t media_catalog_load(int index, media_loaded_track_t *out_loaded);
const anlz_metadata_t *media_catalog_get_loaded_anlz(void);
const anlz_metadata_t *media_catalog_get_loaded_anlz_for_source(media_source_t source);
void media_catalog_free_loaded_anlz(void);
