#include "media_catalog.h"

#include "cdj_link_client.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "library.h"
#include "remote_cache.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "media_catalog";
static media_source_t s_source = MEDIA_SOURCE_LOCAL_USB;
static cdj_link_track_record_t *s_remote_records;
static int s_remote_count;
static anlz_metadata_t s_remote_meta;
static bool s_remote_meta_valid;

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    dst[0] = '\0';
    if (!src) return;
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

esp_err_t media_catalog_init(void)
{
    s_source = MEDIA_SOURCE_LOCAL_USB;
    return ESP_OK;
}

void media_catalog_set_source(media_source_t source)
{
    if (source != MEDIA_SOURCE_REMOTE_LINK) {
        source = MEDIA_SOURCE_LOCAL_USB;
    }
    s_source = source;
}

media_source_t media_catalog_get_source(void)
{
    return s_source;
}

void media_catalog_free_loaded_anlz(void)
{
    if (s_remote_meta_valid) {
        anlz_free(&s_remote_meta);
        s_remote_meta_valid = false;
    }
}

esp_err_t media_catalog_refresh_remote(void)
{
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    esp_err_t rc = cdj_link_client_fetch_library(&blob, &blob_len);
    if (rc != ESP_OK) {
        return rc;
    }

    cdj_link_library_view_t view;
    if (cdj_link_library_decode(blob, blob_len, &view) != CDJ_LINK_OK) {
        free(blob);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cdj_link_track_record_t *records = heap_caps_malloc(view.count * sizeof(*records),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!records) {
        records = malloc(view.count * sizeof(*records));
    }
    if (!records && view.count > 0) {
        free(blob);
        return ESP_ERR_NO_MEM;
    }
    memcpy(records, view.records, view.count * sizeof(*records));
    free(blob);

    free(s_remote_records);
    s_remote_records = records;
    s_remote_count = (int)view.count;
    ESP_LOGI(TAG, "remote catalog ready: %d tracks", s_remote_count);
    return ESP_OK;
}

int media_catalog_count(void)
{
    return s_source == MEDIA_SOURCE_REMOTE_LINK ? s_remote_count : library_count();
}

esp_err_t media_catalog_get(int index, media_catalog_track_t *out_track)
{
    if (!out_track || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_track, 0, sizeof(*out_track));

    if (s_source == MEDIA_SOURCE_REMOTE_LINK) {
        if (index >= s_remote_count || !s_remote_records) {
            return ESP_ERR_NOT_FOUND;
        }
        *out_track = s_remote_records[index];
        return ESP_OK;
    }

    library_track_t track;
    if (library_get(index, &track) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    out_track->track_key = cdj_link_track_key(track.track_id, track.path);
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
    memset(out_row, 0, sizeof(*out_row));

    if (s_source == MEDIA_SOURCE_REMOTE_LINK) {
        if (index >= s_remote_count || !s_remote_records) {
            return ESP_ERR_NOT_FOUND;
        }
        const cdj_link_track_record_t *record = &s_remote_records[index];
        out_row->track_key = record->track_key;
        out_row->bpm = record->bpm;
        out_row->duration_ms = record->duration_ms;
        copy_str(out_row->title, sizeof(out_row->title), record->title);
        copy_str(out_row->artist, sizeof(out_row->artist), record->artist);
        return ESP_OK;
    }

    library_track_t *track = library_get_ptr(index);
    if (!track) {
        return ESP_ERR_NOT_FOUND;
    }
    out_row->track_key = cdj_link_track_key(track->track_id, track->path);
    out_row->bpm = track->bpm;
    out_row->duration_ms = track->duration_ms;
    copy_str(out_row->title, sizeof(out_row->title), track->title);
    copy_str(out_row->artist, sizeof(out_row->artist), track->artist);
    return ESP_OK;
}

static int compare_artist_asc(const void *a, const void *b)
{
    const cdj_link_track_record_t *ta = (const cdj_link_track_record_t *)a;
    const cdj_link_track_record_t *tb = (const cdj_link_track_record_t *)b;
    int c = strcasecmp(ta->artist, tb->artist);
    return c ? c : strcasecmp(ta->title, tb->title);
}

static int compare_title_asc(const void *a, const void *b)
{
    const cdj_link_track_record_t *ta = (const cdj_link_track_record_t *)a;
    const cdj_link_track_record_t *tb = (const cdj_link_track_record_t *)b;
    int c = strcasecmp(ta->title, tb->title);
    return c ? c : strcasecmp(ta->artist, tb->artist);
}

static int compare_bpm_asc(const void *a, const void *b)
{
    const cdj_link_track_record_t *ta = (const cdj_link_track_record_t *)a;
    const cdj_link_track_record_t *tb = (const cdj_link_track_record_t *)b;
    if (ta->bpm != tb->bpm) return (int)ta->bpm - (int)tb->bpm;
    return strcasecmp(ta->title, tb->title);
}

static int compare_artist_desc(const void *a, const void *b) { return compare_artist_asc(b, a); }
static int compare_title_desc(const void *a, const void *b) { return compare_title_asc(b, a); }
static int compare_bpm_desc(const void *a, const void *b) { return compare_bpm_asc(b, a); }

void media_catalog_sort(int field_type, bool descending)
{
    if (s_source == MEDIA_SOURCE_LOCAL_USB) {
        library_sort(field_type, descending);
        return;
    }
    if (!s_remote_records || s_remote_count <= 1) {
        return;
    }
    if (field_type == 0) {
        qsort(s_remote_records, s_remote_count, sizeof(*s_remote_records),
              descending ? compare_artist_desc : compare_artist_asc);
    } else if (field_type == 1) {
        qsort(s_remote_records, s_remote_count, sizeof(*s_remote_records),
              descending ? compare_title_desc : compare_title_asc);
    } else if (field_type == 2) {
        qsort(s_remote_records, s_remote_count, sizeof(*s_remote_records),
              descending ? compare_bpm_desc : compare_bpm_asc);
    }
}

static void ext_path_from_dat(const char *dat_path, char *out, size_t out_len)
{
    copy_str(out, out_len, dat_path);
    char *dot = strrchr(out, '.');
    if (dot) {
        snprintf(dot, out_len - (size_t)(dot - out), ".EXT");
    }
}

static esp_err_t load_local(int index, media_loaded_track_t *out_loaded)
{
    library_track_t *track = heap_caps_calloc(1, sizeof(*track), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!track) {
        track = calloc(1, sizeof(*track));
    }
    if (!track) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t rc = library_get(index, track);
    if (rc != ESP_OK) {
        free(track);
        return ESP_ERR_NOT_FOUND;
    }

    rc = library_load_anlz(track);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "local ANLZ failed: %s", esp_err_to_name(rc));
        free(track);
        return rc;
    }
    rc = library_load_current_anlz(track);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "local current ANLZ failed: %s", esp_err_to_name(rc));
        free(track);
        return rc;
    }

    memset(out_loaded, 0, sizeof(*out_loaded));
    out_loaded->source = MEDIA_SOURCE_LOCAL_USB;
    out_loaded->track_key = cdj_link_track_key(track->track_id, track->path);
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

static esp_err_t load_remote(int index, media_loaded_track_t *out_loaded)
{
    if (index < 0 || index >= s_remote_count || !s_remote_records) {
        return ESP_ERR_NOT_FOUND;
    }

    const cdj_link_track_record_t *record = &s_remote_records[index];
    remote_cache_entry_t cache;
    ESP_RETURN_ON_ERROR(remote_cache_prepare(record->track_key, &cache), TAG, "remote cache");

    media_catalog_free_loaded_anlz();
    ESP_RETURN_ON_ERROR(anlz_parse_dat(cache.dat_path, &s_remote_meta), TAG, "remote DAT");
    s_remote_meta_valid = true;
    if (cache.manifest.has_ext) {
        anlz_parse_ext(cache.ext_path, &s_remote_meta);
    }

    memset(out_loaded, 0, sizeof(*out_loaded));
    out_loaded->source = MEDIA_SOURCE_REMOTE_LINK;
    out_loaded->track_key = record->track_key;
    copy_str(out_loaded->audio_path, sizeof(out_loaded->audio_path), cache.audio_path);
    copy_str(out_loaded->dat_path, sizeof(out_loaded->dat_path), cache.dat_path);
    copy_str(out_loaded->ext_path, sizeof(out_loaded->ext_path), cache.ext_path);
    out_loaded->duration_ms = record->duration_ms;
    out_loaded->bpm = record->bpm;

    if (s_remote_meta.bpm > 0) out_loaded->bpm = s_remote_meta.bpm;
    if (s_remote_meta.beat_count > 0) {
        out_loaded->duration_ms = s_remote_meta.beats[s_remote_meta.beat_count - 1].time_ms;
    }
    if (s_remote_meta.has_waveform_low) {
        memcpy(out_loaded->waveform_low, s_remote_meta.waveform_low, sizeof(out_loaded->waveform_low));
        out_loaded->has_waveform = 1;
    }
    if (s_remote_meta.has_vbr) {
        memcpy(out_loaded->pvbr, s_remote_meta.vbr, sizeof(out_loaded->pvbr));
        out_loaded->has_pvbr = 1;
    }
    return ESP_OK;
}

esp_err_t media_catalog_load(int index, media_loaded_track_t *out_loaded)
{
    if (!out_loaded) {
        return ESP_ERR_INVALID_ARG;
    }
    return s_source == MEDIA_SOURCE_REMOTE_LINK ? load_remote(index, out_loaded)
                                                : load_local(index, out_loaded);
}

const anlz_metadata_t *media_catalog_get_loaded_anlz(void)
{
    return media_catalog_get_loaded_anlz_for_source(s_source);
}

const anlz_metadata_t *media_catalog_get_loaded_anlz_for_source(media_source_t source)
{
    if (source == MEDIA_SOURCE_REMOTE_LINK) {
        return s_remote_meta_valid ? &s_remote_meta : NULL;
    }
    return library_get_current_anlz();
}
