/*
 * Preserve the authoritative PDB/audio duration when ANLZ metadata is applied.
 *
 * The legacy implementation enriches a track in place and historically replaced
 * duration_ms with the timestamp of the final beat. That truncates any outro
 * after the beatgrid. Keep the existing parser/publish path, but restore a
 * nonzero catalog duration before the public call returns. Beatgrid duration
 * therefore remains a fallback only when PDB/audio duration is unavailable.
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
