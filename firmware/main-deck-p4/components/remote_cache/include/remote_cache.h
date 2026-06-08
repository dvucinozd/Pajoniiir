#pragma once

#include <stdint.h>
#include "cdj_link_protocol.h"
#include "esp_err.h"

typedef struct {
    cdj_link_track_manifest_t manifest;
    char dir_path[160];
    char manifest_path[192];
    char audio_path[192];
    char dat_path[192];
    char ext_path[192];
} remote_cache_entry_t;

typedef struct {
    uint64_t bytes;
    uint32_t files;
    uint32_t tracks;
} remote_cache_stats_t;

esp_err_t remote_cache_prepare(uint32_t track_key, remote_cache_entry_t *out_entry);
esp_err_t remote_cache_get_stats(remote_cache_stats_t *out_stats);
esp_err_t remote_cache_clear(void);
uint8_t remote_cache_progress(void);
const char *remote_cache_status(void);
