/* Serialize every bounded SD cache transaction with recorder/log/profile I/O. */
#include "sd_io_gate.h"

#define track_meta_cache_load track_meta_cache_load_ungated
#define track_meta_cache_save track_meta_cache_save_ungated
#include "track_meta_cache.c"
#undef track_meta_cache_load
#undef track_meta_cache_save

esp_err_t track_meta_cache_load(uint32_t track_key,
                                 const char *dat_path,
                                 const char *ext_path,
                                 bool include_high_waveform,
                                 anlz_metadata_t *out_meta)
{
    sd_io_gate_begin();
    esp_err_t rc = track_meta_cache_load_ungated(track_key, dat_path, ext_path,
                                                  include_high_waveform, out_meta);
    sd_io_gate_end();
    return rc;
}

esp_err_t track_meta_cache_save(uint32_t track_key,
                                 const char *dat_path,
                                 const char *ext_path,
                                 const anlz_metadata_t *meta)
{
    sd_io_gate_begin();
    esp_err_t rc = track_meta_cache_save_ungated(track_key, dat_path, ext_path, meta);
    sd_io_gate_end();
    return rc;
}
