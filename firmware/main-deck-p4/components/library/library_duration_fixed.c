/*
 * Preserve the authoritative PDB/audio duration when ANLZ metadata is applied.
 * The selected-row API now lives directly in library.c under production names,
 * so this wrapper only intercepts the legacy duration-overwrite implementation.
 */
#define library_load_anlz library_load_anlz_legacy_duration_override
#include "library.c"
#undef library_load_anlz

esp_err_t library_load_anlz(library_track_t *track)
{
    if (!track) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t catalog_duration_ms = track->duration_ms;
    esp_err_t rc = library_load_anlz_legacy_duration_override(track);
    if (rc == ESP_OK && catalog_duration_ms != 0u) {
        track->duration_ms = catalog_duration_ms;
    }
    return rc;
}
