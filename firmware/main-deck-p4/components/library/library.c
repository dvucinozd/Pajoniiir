#include "library.h"
#include "rekordbox_pdb.h"
#include "rekordbox_anlz.h"
#include "track_meta_cache.h"
#include "media_io_gate.h"
#include "sd_diag_log.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "library";

/* USB drive mount point — set by USB host VFS when the drive is mounted */
#define USB_MOUNT_POINT  "/usb"
#define USB_PDB_PATH     USB_MOUNT_POINT "/PIONEER/rekordbox/export.pdb"

/* Track index — heap-allocated in library_init() to avoid large BSS.
 * Prefers SPIRAM (8 MB OPI PSRAM on JC4880) via heap_caps_malloc();
 * falls back to internal heap if SPIRAM is unavailable. */
#define LIBRARY_MAX_TRACKS  1024

static library_track_t *s_index_buf[2] = { NULL, NULL };
static int              s_active_buf = 0;
static int              s_track_count = 0;
static uint32_t         s_generation = 0;
static SemaphoreHandle_t s_library_mutex = NULL;

static anlz_metadata_t s_current_meta;
static bool            s_current_meta_valid = false;
static int             s_ui_track_idx = 0;

static void library_copy_str(char *dst, size_t dst_len, const char *src)
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

static esp_err_t ensure_library_mutex(void)
{
    if (s_library_mutex) return ESP_OK;
    s_library_mutex = xSemaphoreCreateRecursiveMutex();
    return s_library_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t ensure_index_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_index_buf[i]) continue;
        s_index_buf[i] = heap_caps_malloc(
            LIBRARY_MAX_TRACKS * sizeof(library_track_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_index_buf[i]) {
            ESP_LOGW(TAG, "SPIRAM unavailable for index buffer %d, using internal heap", i);
            s_index_buf[i] = malloc(LIBRARY_MAX_TRACKS * sizeof(library_track_t));
        }
        if (!s_index_buf[i]) {
            ESP_LOGE(TAG, "Out of memory for track index buffer %d", i);
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static inline library_track_t *active_index(void)
{
    return s_index_buf[s_active_buf];
}

uint32_t library_track_key(const library_track_t *track)
{
    if (!track) {
        return 0;
    }
    if (track->track_id != 0) {
        return track->track_id;
    }

    uint32_t hash = 2166136261u;
    const unsigned char *p = (const unsigned char *)track->path;
    while (*p) {
        hash ^= (uint32_t)(*p++);
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

static void library_build_anlz_paths(const library_track_t *track,
                                     char *dat_path,
                                     size_t dat_len,
                                     char *ext_path,
                                     size_t ext_len)
{
    if (!track || !dat_path || dat_len == 0 || !ext_path || ext_len == 0) {
        return;
    }
    if (track->anlz_path[0] == '/') {
        snprintf(dat_path, dat_len, "%s%s", USB_MOUNT_POINT, track->anlz_path);
    } else {
        snprintf(dat_path, dat_len, "%s", track->anlz_path);
    }

    library_copy_str(ext_path, ext_len, dat_path);
    char *dot = strrchr(ext_path, '.');
    if (dot) {
        snprintf(dot, ext_len - (size_t)(dot - ext_path), ".EXT");
    }
}

static void library_apply_meta_to_track(library_track_t *track, const anlz_metadata_t *meta)
{
    if (!track || !meta) {
        return;
    }
    if (meta->bpm > 0) {
        track->bpm = meta->bpm;
    }
    if (meta->beat_count > 0 && meta->beats) {
        track->duration_ms = meta->beats[meta->beat_count - 1].time_ms;
    }
    if (meta->has_waveform_low) {
        memcpy(track->waveform_low, meta->waveform_low, ANLZ_WAVEFORM_LOW_LEN);
        track->has_waveform = 1;
    }
    if (meta->has_vbr) {
        memcpy(track->pvbr, meta->vbr, ANLZ_VBR_TABLE_LEN * sizeof(uint32_t));
        track->has_pvbr = 1;
    }
    track->has_anlz = 1;
}

/* ── library_init ─────────────────────────────────────────────────────────── *
 *
 * Opens export.pdb and builds a fresh inactive index before publishing it.
 *
 * The PDB provides:
 *   • audio file path  (file_path)
 *   • ANLZ file path   (anlz_path) — direct, no directory-walking needed
 *   • title, artist, album
 *   • BPM (coarse, from PDB tempo field)
 *
 * Precise BPM, beat-grid, cues, and waveforms are loaded later on-demand
 * via library_load_anlz().
 */
esp_err_t library_init(void)
{
    ESP_RETURN_ON_ERROR(ensure_library_mutex(), TAG, "library mutex");
    ESP_RETURN_ON_ERROR(ensure_index_buffers(), TAG, "index buffers");

    int build_buf = s_active_buf ^ 1;
    library_track_t *build_index = s_index_buf[build_buf];
    int build_count = 0;
    memset(build_index, 0, LIBRARY_MAX_TRACKS * sizeof(library_track_t));

    pdb_t *pdb = NULL;
    media_io_gate_begin();
    esp_err_t rc = pdb_open(USB_PDB_PATH, &pdb);
    if (rc != ESP_OK) {
        media_io_gate_end();
        ESP_LOGW(TAG, "PDB not found at %s (USB not mounted?)", USB_PDB_PATH);
        return rc;
    }

    int n = pdb_track_count(pdb);
    if (n > LIBRARY_MAX_TRACKS) n = LIBRARY_MAX_TRACKS;

    for (int i = 0; i < n; i++) {
        pdb_track_t pt;
        if (pdb_get_track(pdb, i, &pt) != ESP_OK) continue;

        library_track_t *lt = &build_index[build_count];
        memset(lt, 0, sizeof(*lt));

        library_copy_str(lt->path,      sizeof(lt->path),      pt.file_path);
        library_copy_str(lt->anlz_path, sizeof(lt->anlz_path), pt.anlz_path);
        library_copy_str(lt->title,     sizeof(lt->title),     pt.title);
        library_copy_str(lt->artist,    sizeof(lt->artist),    pt.artist);
        library_copy_str(lt->album,     sizeof(lt->album),     pt.album);
        lt->track_id = pt.track_id;
        lt->bpm      = pt.bpm;
        lt->duration_ms = (uint32_t)pt.duration_s * 1000u;
        library_copy_str(lt->key, sizeof(lt->key), pt.key);

        build_count++;
    }

    pdb_close(pdb);
    media_io_gate_end();

    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    s_active_buf = build_buf;
    s_track_count = build_count;
    s_generation++;
    xSemaphoreGiveRecursive(s_library_mutex);

    ESP_LOGI(TAG, "Library ready: %d tracks from PDB", build_count);
    return ESP_OK;
}

/* ── library_count ────────────────────────────────────────────────────────── */

int library_count(void)
{
    if (ensure_library_mutex() != ESP_OK) return 0;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    int count = s_track_count;
    xSemaphoreGiveRecursive(s_library_mutex);
    return count;
}

uint32_t library_generation(void)
{
    if (ensure_library_mutex() != ESP_OK) return 0;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    uint32_t gen = s_generation;
    xSemaphoreGiveRecursive(s_library_mutex);
    return gen;
}

void library_clear(void)
{
    if (ensure_library_mutex() != ESP_OK) return;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (s_current_meta_valid) {
        anlz_free(&s_current_meta);
        s_current_meta_valid = false;
    }
    s_track_count = 0;
    s_generation++;
    s_ui_track_idx = 0;
    xSemaphoreGiveRecursive(s_library_mutex);
    ESP_LOGI(TAG, "Library cleared");
}

/* ── library_get ──────────────────────────────────────────────────────────── */

esp_err_t library_get(int index, library_track_t *out)
{
    if (!out || ensure_library_mutex() != ESP_OK) return ESP_ERR_NOT_FOUND;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_index();
    if (!idx || index < 0 || index >= s_track_count) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *out = idx[index];
    xSemaphoreGiveRecursive(s_library_mutex);
    return ESP_OK;
}

/* ── library_get_ptr (simulator only) ─────────────────────────────────────── *
 *
 * Returns a pointer into the live index, which library_init()/library_sort()
 * republish under the caller's feet. The single-threaded PC simulator can
 * live with that; firmware code must use library_get()/library_get_summary()
 * (or media_catalog) instead, so the symbol does not exist there.
 */
#ifdef WIN32
library_track_t *library_get_ptr(int index)
{
    if (ensure_library_mutex() != ESP_OK) return NULL;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_index();
    library_track_t *track = (!idx || index < 0 || index >= s_track_count) ? NULL : &idx[index];
    xSemaphoreGiveRecursive(s_library_mutex);
    return track;
}
#endif

/* ── library_get_summary ──────────────────────────────────────────────────── */

esp_err_t library_get_summary(int index, uint16_t *out_bpm, uint32_t *out_duration_ms)
{
    if (ensure_library_mutex() != ESP_OK) return ESP_ERR_NOT_FOUND;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_index();
    if (!idx || index < 0 || index >= s_track_count) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    if (out_bpm) *out_bpm = idx[index].bpm;
    if (out_duration_ms) *out_duration_ms = idx[index].duration_ms;
    xSemaphoreGiveRecursive(s_library_mutex);
    return ESP_OK;
}

/* ── library_load_anlz ────────────────────────────────────────────────────── *
 *
 * Loads ANLZ metadata for a track using its anlz_path directly.
 * Populates track->bpm (precise), track->duration_ms, track->has_anlz.
 *
 * Requires the USB drive to be mounted.
 */
esp_err_t library_load_anlz(library_track_t *track)
{
    if (!track) return ESP_ERR_INVALID_ARG;

    if (track->anlz_path[0] == '\0') {
        ESP_LOGE(TAG, "No ANLZ path for track: %s", track->title);
        return ESP_ERR_NOT_FOUND;
    }

    char dat_path[LIBRARY_PATH_MAX + 8];
    char ext_path[LIBRARY_PATH_MAX + 8];
    library_build_anlz_paths(track, dat_path, sizeof(dat_path), ext_path, sizeof(ext_path));
    uint32_t track_key = library_track_key(track);

    anlz_metadata_t cached;
    esp_err_t cache_rc = track_meta_cache_load(track_key, dat_path, ext_path, false, &cached);
    if (cache_rc == ESP_OK) {
        library_apply_meta_to_track(track, &cached);
        anlz_free(&cached);
        sd_diag_log_write("meta_cache", "local summary cache hit");
        return ESP_OK;
    }

    media_io_gate_begin();

    /* Parse DAT */
    anlz_metadata_t meta;
    esp_err_t rc = anlz_parse_dat(dat_path, &meta);
    if (rc != ESP_OK) {
        media_io_gate_end();
        ESP_LOGE(TAG, "anlz_parse_dat failed: %s", dat_path);
        return rc;
    }

    library_apply_meta_to_track(track, &meta);

    /* Parse EXT for high-res waveform (best-effort, ignore failure) */
    anlz_parse_ext(ext_path, &meta);
    media_io_gate_end();
    esp_err_t save_rc = track_meta_cache_save(track_key, dat_path, ext_path, &meta);
    sd_diag_log_write("meta_cache", save_rc == ESP_OK ? "local summary cache write" : "local summary cache write failed");

    ESP_LOGI(TAG, "ANLZ: \"%s\" bpm=%u dur=%lums cues=%u hi-wav=%u",
             track->title[0] ? track->title : track->path,
             track->bpm, (unsigned long)track->duration_ms,
             meta.cue_count, meta.waveform_high_len);

    anlz_free(&meta);
    return ESP_OK;
}

/* ── Active Track ANLZ Management ─────────────────────────────────────────── */

void library_free_current_anlz(void)
{
    if (ensure_library_mutex() != ESP_OK) return;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (s_current_meta_valid) {
        anlz_free(&s_current_meta);
        s_current_meta_valid = false;
        ESP_LOGI(TAG, "Freed currently loaded track ANLZ metadata");
    }
    xSemaphoreGiveRecursive(s_library_mutex);
}

esp_err_t library_load_current_anlz(const library_track_t *track)
{
    if (!track) return ESP_ERR_INVALID_ARG;

    /* First, free the previous track's ANLZ metadata */
    library_free_current_anlz();

    if (track->anlz_path[0] == '\0') {
        ESP_LOGE(TAG, "No ANLZ path for track: %s", track->title);
        return ESP_ERR_NOT_FOUND;
    }

    char dat_path[LIBRARY_PATH_MAX + 8];
    char ext_path[LIBRARY_PATH_MAX + 8];
    library_build_anlz_paths(track, dat_path, sizeof(dat_path), ext_path, sizeof(ext_path));
    uint32_t track_key = library_track_key(track);

    anlz_metadata_t cached;
    esp_err_t cache_rc = track_meta_cache_load(track_key, dat_path, ext_path, true, &cached);
    if (cache_rc == ESP_OK) {
        xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
        s_current_meta = cached;
        s_current_meta_valid = true;
        xSemaphoreGiveRecursive(s_library_mutex);

        sd_diag_log_write("meta_cache", "current track cache hit");
        ESP_LOGI(TAG, "Loaded current track ANLZ from cache: \"%s\" (cues=%u, beats=%u, hi-wav=%u)",
                 track->title[0] ? track->title : track->path,
                 s_current_meta.cue_count, s_current_meta.beat_count, s_current_meta.waveform_high_len);
        return ESP_OK;
    }

    media_io_gate_begin();

    /* Parse DAT */
    anlz_metadata_t meta;
    esp_err_t rc = anlz_parse_dat(dat_path, &meta);
    if (rc != ESP_OK) {
        media_io_gate_end();
        ESP_LOGE(TAG, "Failed to load current ANLZ DAT: %s", dat_path);
        return rc;
    }

    /* Parse EXT (best effort) */
    anlz_parse_ext(ext_path, &meta);
    media_io_gate_end();
    esp_err_t save_rc = track_meta_cache_save(track_key, dat_path, ext_path, &meta);
    sd_diag_log_write("meta_cache", save_rc == ESP_OK ? "current track cache write" : "current track cache write failed");

    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    s_current_meta = meta;
    s_current_meta_valid = true;
    xSemaphoreGiveRecursive(s_library_mutex);

    ESP_LOGI(TAG, "Successfully loaded current track ANLZ: \"%s\" (cues=%u, beats=%u, hi-wav=%u)",
             track->title[0] ? track->title : track->path,
             s_current_meta.cue_count, s_current_meta.beat_count, s_current_meta.waveform_high_len);

    return ESP_OK;
}

const anlz_metadata_t *library_get_current_anlz(void)
{
    if (ensure_library_mutex() != ESP_OK) return NULL;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    const anlz_metadata_t *meta = NULL;
    if (s_current_meta_valid) {
        meta = &s_current_meta;
    }
    xSemaphoreGiveRecursive(s_library_mutex);
    return meta;
}

static int compare_artist_asc(const void *a, const void *b)
{
    const library_track_t *ta = (const library_track_t *)a;
    const library_track_t *tb = (const library_track_t *)b;
    int cmp = strcasecmp(ta->artist, tb->artist);
    if (cmp == 0) {
        return strcasecmp(ta->title, tb->title);
    }
    return cmp;
}

static int compare_artist_desc(const void *a, const void *b)
{
    return compare_artist_asc(b, a);
}

static int compare_title_asc(const void *a, const void *b)
{
    const library_track_t *ta = (const library_track_t *)a;
    const library_track_t *tb = (const library_track_t *)b;
    int cmp = strcasecmp(ta->title, tb->title);
    if (cmp == 0) {
        return strcasecmp(ta->artist, tb->artist);
    }
    return cmp;
}

static int compare_title_desc(const void *a, const void *b)
{
    return compare_title_asc(b, a);
}

static int compare_bpm_asc(const void *a, const void *b)
{
    const library_track_t *ta = (const library_track_t *)a;
    const library_track_t *tb = (const library_track_t *)b;
    if (ta->bpm != tb->bpm) {
        return (int)ta->bpm - (int)tb->bpm;
    }
    return strcasecmp(ta->title, tb->title);
}

static int compare_bpm_desc(const void *a, const void *b)
{
    return compare_bpm_asc(b, a);
}

/* Camelot keys ("8A", "12B") need numeric-aware ordering: plain strcasecmp
 * puts "10A" before "2A". Classical names ("Am", "F#m") fall back to a
 * string compare, and tracks without a key sort after keyed ones. */
static bool parse_camelot_key(const char *key, int *out_number, char *out_letter)
{
    int number = 0;
    int i = 0;
    while (key[i] >= '0' && key[i] <= '9' && i < 2) {
        number = number * 10 + (key[i] - '0');
        i++;
    }
    if (i == 0 || number < 1 || number > 12) {
        return false;
    }
    char letter = key[i];
    if (letter >= 'a' && letter <= 'z') {
        letter = (char)(letter - 'a' + 'A');
    }
    if ((letter != 'A' && letter != 'B') || key[i + 1] != '\0') {
        return false;
    }
    *out_number = number;
    *out_letter = letter;
    return true;
}

static int compare_key_asc(const void *a, const void *b)
{
    const library_track_t *ta = (const library_track_t *)a;
    const library_track_t *tb = (const library_track_t *)b;

    bool a_empty = ta->key[0] == '\0';
    bool b_empty = tb->key[0] == '\0';
    if (a_empty != b_empty) {
        return a_empty ? 1 : -1;
    }

    int cmp = 0;
    int num_a = 0, num_b = 0;
    char let_a = 0, let_b = 0;
    if (parse_camelot_key(ta->key, &num_a, &let_a) &&
        parse_camelot_key(tb->key, &num_b, &let_b)) {
        cmp = (num_a != num_b) ? (num_a - num_b) : (let_a - let_b);
    } else {
        cmp = strcasecmp(ta->key, tb->key);
    }
    if (cmp == 0) {
        return strcasecmp(ta->title, tb->title);
    }
    return cmp;
}

static int compare_key_desc(const void *a, const void *b)
{
    return compare_key_asc(b, a);
}

void library_sort(int field_type, bool descending)
{
    if (ensure_library_mutex() != ESP_OK) return;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *src = active_index();
    if (!src || s_track_count <= 1) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return;
    }

    /* Publish-on-write: sort a copy in the inactive buffer and flip, so any
     * reader of the previous buffer sees a stable snapshot instead of rows
     * being reshuffled mid-read by an in-place qsort. */
    int build_buf = s_active_buf ^ 1;
    library_track_t *idx = s_index_buf[build_buf];
    if (!idx) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return;
    }
    memcpy(idx, src, (size_t)s_track_count * sizeof(library_track_t));

    if (field_type == 0) { // Artist
        qsort(idx, s_track_count, sizeof(library_track_t), descending ? compare_artist_desc : compare_artist_asc);
    } else if (field_type == 1) { // Title / Name
        qsort(idx, s_track_count, sizeof(library_track_t), descending ? compare_title_desc : compare_title_asc);
    } else if (field_type == 2) { // BPM
        qsort(idx, s_track_count, sizeof(library_track_t), descending ? compare_bpm_desc : compare_bpm_asc);
    } else if (field_type == 3) { // Key
        qsort(idx, s_track_count, sizeof(library_track_t), descending ? compare_key_desc : compare_key_asc);
    }
    s_active_buf = build_buf;
    s_generation++;
    xSemaphoreGiveRecursive(s_library_mutex);
    ESP_LOGI(TAG, "Library sorted: field=%d, descending=%d", field_type, descending);
}

/* ── UI track selection stubs (firmware only) ─────────────────────────────── *
 *
 * In the PC simulator these functions are fully implemented in mocks.c.
 * On the real ESP32-P4 they provide a minimal index tracker so the UI can
 * call library_get(mock_library_get_current_track_index(), &track) without
 * knowing whether it's running on hardware or in the simulator.
 *
 * Phase 8/9: replace with a proper deck_core track-load API.
 */
void mock_library_load_track_to_deck(int track_index)
{
    if (ensure_library_mutex() != ESP_OK) return;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (track_index >= 0 && track_index < s_track_count) {
        s_ui_track_idx = track_index;
    }
    xSemaphoreGiveRecursive(s_library_mutex);
}

int mock_library_get_current_track_index(void)
{
    if (ensure_library_mutex() != ESP_OK) return 0;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    int idx = s_ui_track_idx;
    xSemaphoreGiveRecursive(s_library_mutex);
    return idx;
}
