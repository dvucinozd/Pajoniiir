#include "media_catalog.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "library.h"
#include "service_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "media_catalog";

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t i = 0;
    while (i + 1u < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ext_path_from_dat(const char *dat_path, char *out, size_t out_len)
{
    copy_str(out, out_len, dat_path);
    char *dot = strrchr(out, '.');
    if (dot) {
        snprintf(dot, out_len - (size_t)(dot - out), ".EXT");
    }
}

esp_err_t media_catalog_init(void)
{
    return ESP_OK;
}

int media_catalog_count(void)
{
    return library_count();
}

esp_err_t media_catalog_get(int index, media_catalog_track_t *out_track)
{
    if (!out_track || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    library_track_t track;
    if (library_get(index, &track) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(out_track, 0, sizeof(*out_track));
    out_track->track_key = library_track_key(&track);
    out_track->rekordbox_track_id = track.track_id;
    out_track->bpm = track.bpm;
    out_track->duration_ms = track.duration_ms;
    copy_str(out_track->title, sizeof(out_track->title), track.title);
    copy_str(out_track->artist, sizeof(out_track->artist), track.artist);
    copy_str(out_track->album, sizeof(out_track->album), track.album);
    return ESP_OK;
}

esp_err_t media_catalog_get_row(int index, media_catalog_row_t *out_row)
{
    if (!out_row || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    library_track_t track;
    if (library_get(index, &track) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(out_row, 0, sizeof(*out_row));
    out_row->track_key = library_track_key(&track);
    out_row->bpm = track.bpm;
    out_row->duration_ms = track.duration_ms;
    copy_str(out_row->title, sizeof(out_row->title), track.title);
    copy_str(out_row->artist, sizeof(out_row->artist), track.artist);
    copy_str(out_row->key, sizeof(out_row->key), track.key);
    return ESP_OK;
}

void media_catalog_sort(int field_type, bool descending)
{
    library_sort(field_type, descending);
}

esp_err_t media_catalog_load(int index, media_loaded_track_t *out_loaded)
{
    if (!out_loaded) {
        return ESP_ERR_INVALID_ARG;
    }

    library_track_t *track = heap_caps_calloc(1, sizeof(*track),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!track) {
        track = calloc(1, sizeof(*track));
    }
    if (!track) {
        return ESP_ERR_NO_MEM;
    }

    service_log_event(SERVICE_LOG_TRACK_LOAD_START, SERVICE_LOG_INFO,
                      1u, (uint32_t)index, 0u, 0u, 0u, NULL);

    esp_err_t rc = library_get(index, track);
    if (rc != ESP_OK) {
        service_log_event(SERVICE_LOG_TRACK_LOAD_FAILED, SERVICE_LOG_WARN,
                          2u, (uint32_t)index, (uint32_t)rc, 0u, 0u, "library_get");
        free(track);
        return ESP_ERR_NOT_FOUND;
    }

    rc = library_load_anlz(track);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "local ANLZ failed: %s", esp_err_to_name(rc));
        service_log_event(SERVICE_LOG_TRACK_LOAD_FAILED, SERVICE_LOG_WARN,
                          2u, (uint32_t)index, (uint32_t)rc, 0u, 0u, "anlz");
        free(track);
        return rc;
    }

    {
        uint32_t meta_ms = 0u, cache_written = 0u;
        uint8_t source = 0u;
        bool written = false;
        library_last_anlz_load_stats(&meta_ms, &source, &written);
        cache_written = written ? 1u : 0u;
        service_log_event(SERVICE_LOG_TRACK_LOAD_DONE, SERVICE_LOG_INFO,
                          4u, library_track_key(track), (uint32_t)source,
                          meta_ms, cache_written,
                          track->title[0] ? track->title : track->path);
    }

    memset(out_loaded, 0, sizeof(*out_loaded));
    out_loaded->track_key = library_track_key(track);
    snprintf(out_loaded->audio_path, sizeof(out_loaded->audio_path), "/usb%s", track->path);
    if (track->anlz_path[0] == '/') {
        snprintf(out_loaded->dat_path, sizeof(out_loaded->dat_path), "/usb%s", track->anlz_path);
    } else {
        copy_str(out_loaded->dat_path, sizeof(out_loaded->dat_path), track->anlz_path);
    }
    ext_path_from_dat(out_loaded->dat_path, out_loaded->ext_path, sizeof(out_loaded->ext_path));
    out_loaded->duration_ms = track->duration_ms;
    out_loaded->bpm = track->bpm;
    out_loaded->has_waveform = track->has_waveform;
    memcpy(out_loaded->waveform_low, track->waveform_low, sizeof(out_loaded->waveform_low));
    out_loaded->has_pvbr = track->has_pvbr;
    memcpy(out_loaded->pvbr, track->pvbr, sizeof(out_loaded->pvbr));
    free(track);
    return ESP_OK;
}

esp_err_t media_catalog_clone_loaded_anlz(anlz_metadata_t *out)
{
    return library_clone_current_anlz(out);
}
