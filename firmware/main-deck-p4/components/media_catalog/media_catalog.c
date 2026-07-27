#include "media_catalog.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "library.h"
#include "service_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "media_catalog";

static SemaphoreHandle_t s_catalog_mutex;
static portMUX_TYPE s_catalog_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_load_in_progress;

static SemaphoreHandle_t catalog_mutex(void)
{
    if (s_catalog_mutex) {
        return s_catalog_mutex;
    }

    SemaphoreHandle_t created = xSemaphoreCreateMutex();
    if (!created) {
        return NULL;
    }

    taskENTER_CRITICAL(&s_catalog_mutex_init_lock);
    if (!s_catalog_mutex) {
        s_catalog_mutex = created;
        created = NULL;
    }
    taskEXIT_CRITICAL(&s_catalog_mutex_init_lock);

    if (created) {
        vSemaphoreDelete(created);
    }
    return s_catalog_mutex;
}

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

static void fill_catalog_track(const library_track_t *track,
                               media_catalog_track_t *out_track)
{
    if (!track || !out_track) {
        return;
    }
    memset(out_track, 0, sizeof(*out_track));
    out_track->track_key = library_track_key(track);
    out_track->rekordbox_track_id = track->track_id;
    out_track->bpm = track->bpm;
    out_track->duration_ms = track->duration_ms;
    copy_str(out_track->title, sizeof(out_track->title), track->title);
    copy_str(out_track->artist, sizeof(out_track->artist), track->artist);
    copy_str(out_track->album, sizeof(out_track->album), track->album);
}

static void fill_loaded_track(const library_track_t *track,
                              media_loaded_track_t *out_loaded)
{
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
}

esp_err_t media_catalog_init(void)
{
    return catalog_mutex() ? ESP_OK : ESP_ERR_NO_MEM;
}

int media_catalog_count(void)
{
    return library_count();
}

uint32_t media_catalog_generation(void)
{
    return library_generation();
}

bool media_catalog_load_in_progress(void)
{
    return s_load_in_progress;
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

    fill_catalog_track(&track, out_track);
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
    SemaphoreHandle_t mutex = catalog_mutex();
    if (!mutex) {
        ESP_LOGE(TAG, "catalog sort skipped: mutex allocation failed");
        return;
    }
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    library_sort(field_type, descending);
    xSemaphoreGive(mutex);
}

esp_err_t media_catalog_load_by_identity(uint32_t track_key,
                                         uint32_t expected_generation,
                                         media_catalog_track_t *out_track,
                                         media_loaded_track_t *out_loaded)
{
    if (track_key == 0u || !out_loaded) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mutex = catalog_mutex();
    if (!mutex) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_load_in_progress = true;
    esp_err_t result = ESP_OK;
    library_track_t *track = NULL;

    if (library_generation() != expected_generation) {
        result = ESP_ERR_INVALID_STATE;
        goto done;
    }

    track = heap_caps_calloc(1, sizeof(*track), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!track) {
        track = calloc(1, sizeof(*track));
    }
    if (!track) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }

    bool found = false;
    const int count = library_count();
    for (int index = 0; index < count; ++index) {
        if (library_get(index, track) == ESP_OK && library_track_key(track) == track_key) {
            found = true;
            break;
        }
    }
    if (!found) {
        result = library_generation() == expected_generation
                     ? ESP_ERR_NOT_FOUND
                     : ESP_ERR_INVALID_STATE;
        goto done;
    }
    if (library_generation() != expected_generation) {
        result = ESP_ERR_INVALID_STATE;
        goto done;
    }

    service_log_event(SERVICE_LOG_TRACK_LOAD_START, SERVICE_LOG_INFO,
                      1u, track_key, 0u, 0u, 0u, NULL);

    /* The PDB/audio duration includes any outro after the final beat. Preserve
     * it across ANLZ enrichment; beatgrid duration is only a fallback when the
     * catalog itself has no duration. */
    const uint32_t catalog_duration_ms = track->duration_ms;

    /* Analysis data refines the PDB row but is not required for playback. */
    bool anlz_ok = true;
    esp_err_t anlz_rc = library_load_anlz(track);
    if (catalog_duration_ms != 0u) {
        track->duration_ms = catalog_duration_ms;
    }
    if (anlz_rc != ESP_OK) {
        anlz_ok = false;
        ESP_LOGW(TAG, "no analysis data for track 0x%08x (%s); loading without it",
                 (unsigned)track_key, esp_err_to_name(anlz_rc));
        service_log_event(SERVICE_LOG_TRACK_ANLZ_MISSING, SERVICE_LOG_WARN,
                          2u, track_key, (uint32_t)anlz_rc, 0u, 0u,
                          track->anlz_path[0] ? "unreadable" : "absent in pdb");
    }

    /* USB removal or catalog replacement may occur outside the sort lock. Never
     * publish a loaded path to the deck if the catalog changed during I/O. */
    if (library_generation() != expected_generation || library_track_key(track) != track_key) {
        result = ESP_ERR_INVALID_STATE;
        goto done;
    }

    uint32_t meta_ms = 0u;
    uint32_t cache_written = 0u;
    uint8_t source = 0u;
    if (anlz_ok) {
        bool written = false;
        library_last_anlz_load_stats(&meta_ms, &source, &written);
        cache_written = written ? 1u : 0u;
    }
    service_log_event(SERVICE_LOG_TRACK_LOAD_DONE, SERVICE_LOG_INFO,
                      4u, track_key, (uint32_t)source, meta_ms, cache_written,
                      track->title[0] ? track->title : track->path);

    if (out_track) {
        fill_catalog_track(track, out_track);
    }
    fill_loaded_track(track, out_loaded);

done:
    if (result != ESP_OK && out_track) {
        memset(out_track, 0, sizeof(*out_track));
    }
    if (result != ESP_OK) {
        memset(out_loaded, 0, sizeof(*out_loaded));
        service_log_event(SERVICE_LOG_TRACK_LOAD_FAILED, SERVICE_LOG_WARN,
                          2u, track_key, (uint32_t)result, 0u, 0u,
                          result == ESP_ERR_INVALID_STATE ? "stale generation" : "identity resolve");
    }
    free(track);
    s_load_in_progress = false;
    xSemaphoreGive(mutex);
    return result;
}

esp_err_t media_catalog_load(int index, media_loaded_track_t *out_loaded)
{
    if (!out_loaded || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t generation = media_catalog_generation();
    media_catalog_track_t item;
    esp_err_t rc = media_catalog_get(index, &item);
    if (rc != ESP_OK) {
        return rc;
    }
    if (media_catalog_generation() != generation) {
        return ESP_ERR_INVALID_STATE;
    }
    return media_catalog_load_by_identity(item.track_key, generation, NULL, out_loaded);
}

esp_err_t media_catalog_clone_loaded_anlz(anlz_metadata_t *out)
{
    return library_clone_current_anlz(out);
}
