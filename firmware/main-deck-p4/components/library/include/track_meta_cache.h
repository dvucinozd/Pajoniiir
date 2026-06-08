#pragma once

#include "esp_err.h"
#include "rekordbox_anlz.h"

#include <stdbool.h>
#include <stdint.h>

esp_err_t track_meta_cache_load(uint32_t track_key,
                                const char *dat_path,
                                const char *ext_path,
                                bool include_high_waveform,
                                anlz_metadata_t *out_meta);

esp_err_t track_meta_cache_save(uint32_t track_key,
                                const char *dat_path,
                                const char *ext_path,
                                const anlz_metadata_t *meta);

