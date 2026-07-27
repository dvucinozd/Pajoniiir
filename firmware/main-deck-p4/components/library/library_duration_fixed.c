/*
 * Preserve the authoritative PDB/audio duration when ANLZ metadata is applied
 * and expose production-named selected-row helpers.
 *
 * The legacy implementation still uses mock_library_* names internally. Rename
 * those definitions at preprocessing time so only the production symbols are
 * emitted; no public/linkable compatibility aliases remain.
 */
#define library_load_anlz                    library_load_anlz_legacy_duration_override
#define mock_library_load_track_to_deck      library_set_selected_track_index
#define mock_library_get_current_track_index library_selected_track_index
#include "library.c"
#undef mock_library_get_current_track_index
#undef mock_library_load_track_to_deck
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
