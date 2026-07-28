#include "library.h"
#include "rekordbox_pdb.h"
#include "rekordbox_anlz.h"
#include "track_meta_cache.h"
#include "media_io_gate.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "library";

/* USB drive mount point — set by USB host VFS when the drive is mounted */
#ifndef WIN32
#define USB_MOUNT_POINT  "/usb"
#else
#define USB_MOUNT_POINT  "C:/Users/klikn/Music/USB"
#endif
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
static bool             s_index_building = false;

static anlz_metadata_t s_current_meta;
static bool            s_current_meta_valid = false;
static int             s_ui_track_idx = 0;

/* Timing/source of the most recent library_load_anlz() resolve, published for
 * the service log's authoritative track-load event. */
static uint32_t        s_last_load_elapsed_ms = 0u;
static uint32_t        s_last_load_source = 0u;        /* 0 = cache, 1 = USB */
static uint32_t        s_last_load_cache_written = 0u; /* 0/1 */

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

    /* Reserve the inactive buffer before doing slow media I/O.  A mount event,
     * startup probe and UI sort may otherwise select and mutate the same
     * buffer concurrently. */
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (s_index_building) {
        xSemaphoreGiveRecursive(s_library_mutex);
        ESP_LOGW(TAG, "Library index build already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = ensure_index_buffers();
    if (rc != ESP_OK) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return rc;
    }
    s_index_building = true;
    int build_buf = s_active_buf ^ 1;
    uint32_t build_generation = s_generation;
    xSemaphoreGiveRecursive(s_library_mutex);

    library_track_t *build_index = s_index_buf[build_buf];
    int build_count = 0;
    memset(build_index, 0, LIBRARY_MAX_TRACKS * sizeof(library_track_t));

    pdb_t *pdb = NULL;
    media_io_gate_begin();
    rc = pdb_open(USB_PDB_PATH, &pdb);
    if (rc != ESP_OK) {
        media_io_gate_end();
        xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
        s_index_building = false;
        xSemaphoreGiveRecursive(s_library_mutex);
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
    if (s_generation != build_generation) {
        /* library_clear() ran while the media was being parsed (for example,
         * because the drive was removed).  Never republish that stale index. */
        s_index_building = false;
        xSemaphoreGiveRecursive(s_library_mutex);
        ESP_LOGW(TAG, "Discarding stale library index build");
        return ESP_ERR_INVALID_STATE;
    }
    s_active_buf = build_buf;
    s_track_count = build_count;
    s_generation++;
    s_index_building = false;
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

/* ── ANLZ resolver ────────────────────────────────────────────────────────── *
 *
 * Resolve one owned, full anlz_metadata_t for a track (high-resolution waveform
 * included). On ESP_OK, *out owns the metadata and the caller must anlz_free it.
 * Reads the metadata cache exactly once; on a miss it parses DAT once and EXT
 * once from the USB medium and performs one best-effort cache write. On any
 * failure *out is left zeroed (track_meta_cache_load zeroes it, and anlz_free of
 * a zeroed object is a no-op) and the USB/cache media state is unchanged.
 */
typedef enum {
    LIBRARY_ANLZ_SRC_CACHE = 0,
    LIBRARY_ANLZ_SRC_USB,
} library_anlz_source_t;

static esp_err_t library_resolve_anlz(const library_track_t *track,
                                      anlz_metadata_t *out,
                                      library_anlz_source_t *source,
                                      bool *cache_written)
{
    if (source) *source = LIBRARY_ANLZ_SRC_USB;
    if (cache_written) *cache_written = false;
    if (!track || !out) return ESP_ERR_INVALID_ARG;

    if (track->anlz_path[0] == '\0') {
        ESP_LOGE(TAG, "No ANLZ path for track: %s", track->title);
        return ESP_ERR_NOT_FOUND;
    }

    char dat_path[LIBRARY_PATH_MAX + 8];
    char ext_path[LIBRARY_PATH_MAX + 8];
    library_build_anlz_paths(track, dat_path, sizeof(dat_path), ext_path, sizeof(ext_path));
    uint32_t track_key = library_track_key(track);

    /* Warm path: a single cache load carrying the high-resolution waveform. */
    esp_err_t cache_rc = track_meta_cache_load(track_key, dat_path, ext_path, true, out);
    if (cache_rc == ESP_OK) {
        if (source) *source = LIBRARY_ANLZ_SRC_CACHE;
        return ESP_OK;
    }

    /* Cold path: parse DAT once and EXT once, then one best-effort cache write.
     * track_meta_cache_load already zeroed *out on its miss return. */
    media_io_gate_begin();
    esp_err_t rc = anlz_parse_dat(dat_path, out);
    if (rc != ESP_OK) {
        media_io_gate_end();
        ESP_LOGE(TAG, "anlz_parse_dat failed: %s", dat_path);
        return rc;
    }
    anlz_parse_ext(ext_path, out);           /* best-effort high-res waveform */
    media_io_gate_end();

    esp_err_t save_rc = track_meta_cache_save(track_key, dat_path, ext_path, out);
    if (cache_written) *cache_written = (save_rc == ESP_OK);
    return ESP_OK;
}

/* ── library_load_anlz ────────────────────────────────────────────────────── *
 *
 * Resolve one authoritative full ANLZ object for a track and use it for both
 * responsibilities in a single pass: populate the track summary fields (precise
 * BPM, duration, low waveform, PVBR table) and publish the same full beat/cue/
 * high-resolution-waveform object as the current metadata.
 *
 * The publish is transactional: the new object is swapped into s_current_meta
 * only after a fully successful resolve, and the previous object is freed after
 * the swap and outside the mutex.
 *
 * A failed resolve retires the published metadata instead of preserving it, and
 * returns the error while leaving the track's PDB-derived summary (path, title,
 * artist, BPM, duration) untouched. The caller is expected to continue loading
 * the track without analysis data: the audio only needs the media path, whereas
 * BPM/beatgrid/waveform/cues are refinements the UI already knows how to omit.
 * Preserving the previous track's object here would be worse than having none,
 * because it would be drawn over the newly loaded track.
 *
 * Requires the USB drive mounted only on a cache miss.
 */
esp_err_t library_load_anlz(library_track_t *track)
{
    if (!track) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ensure_library_mutex(), TAG, "library mutex");

    int64_t t_start = esp_timer_get_time();

    anlz_metadata_t meta;
    library_anlz_source_t source = LIBRARY_ANLZ_SRC_USB;
    bool cache_written = false;
    esp_err_t rc = library_resolve_anlz(track, &meta, &source, &cache_written);
    if (rc != ESP_OK) {
        /* The caller now continues loading this track without analysis data, so
         * the previously published metadata must NOT survive: it belongs to a
         * different track, and leaving it would draw the old waveform, beatgrid
         * and hot cues over the newly loaded one. Retire it with the same
         * swap-then-free-outside-the-lock discipline as a successful publish, so
         * a UI clone in flight is never blocked on a free.
         * library_clone_current_anlz() then reports NOT_FOUND and every consumer
         * takes its existing "no metadata" path. */
        anlz_metadata_t stale_meta;
        bool have_stale = false;
        xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
        if (s_current_meta_valid) {
            stale_meta = s_current_meta;
            have_stale = true;
        }
        s_current_meta_valid = false;
        xSemaphoreGiveRecursive(s_library_mutex);
        if (have_stale) {
            anlz_free(&stale_meta);
        }
        return rc;
    }

    /* Populate the summary fields on the track from the resolved object. This
     * only copies values out of meta; it retains no pointers into it. */
    library_apply_meta_to_track(track, &meta);

    /* Publish transactionally: swap the new object in, capture the old one, then
     * free the old one outside the lock so a UI clone in flight is never blocked
     * on a free. Ownership of meta moves into s_current_meta. */
    anlz_metadata_t old_meta;
    bool have_old = false;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (s_current_meta_valid) {
        old_meta = s_current_meta;
        have_old = true;
    }
    s_current_meta = meta;
    s_current_meta_valid = true;
    xSemaphoreGiveRecursive(s_library_mutex);
    if (have_old) {
        anlz_free(&old_meta);
    }

    int64_t elapsed_us = esp_timer_get_time() - t_start;
    const char *src_name = (source == LIBRARY_ANLZ_SRC_CACHE) ? "cache" : "usb";
    __atomic_store_n(&s_last_load_elapsed_ms, (uint32_t)(elapsed_us / 1000), __ATOMIC_RELAXED);
    __atomic_store_n(&s_last_load_source, (uint32_t)source, __ATOMIC_RELAXED);
    __atomic_store_n(&s_last_load_cache_written, cache_written ? 1u : 0u, __ATOMIC_RELAXED);
    ESP_LOGI(TAG, "ANLZ \"%s\" src=%s bpm=%u dur=%lums cues=%u beats=%u hi-wav=%u %lldus",
             track->title[0] ? track->title : track->path, src_name,
             track->bpm, (unsigned long)track->duration_ms,
             meta.cue_count, meta.beat_count, meta.waveform_high_len,
             (long long)elapsed_us);
    return ESP_OK;
}

void library_last_anlz_load_stats(uint32_t *out_elapsed_ms, uint8_t *out_source,
                                  bool *out_cache_written)
{
    if (out_elapsed_ms) {
        *out_elapsed_ms = __atomic_load_n(&s_last_load_elapsed_ms, __ATOMIC_RELAXED);
    }
    if (out_source) {
        *out_source = (uint8_t)__atomic_load_n(&s_last_load_source, __ATOMIC_RELAXED);
    }
    if (out_cache_written) {
        *out_cache_written = __atomic_load_n(&s_last_load_cache_written, __ATOMIC_RELAXED) != 0u;
    }
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

esp_err_t library_clone_current_anlz(anlz_metadata_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    ESP_RETURN_ON_ERROR(ensure_library_mutex(), TAG, "library mutex");
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    esp_err_t rc = s_current_meta_valid
                       ? anlz_clone(&s_current_meta, out)
                       : ESP_ERR_NOT_FOUND;
    xSemaphoreGiveRecursive(s_library_mutex);
    return rc;
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
    if (s_index_building) {
        xSemaphoreGiveRecursive(s_library_mutex);
        ESP_LOGW(TAG, "Ignoring sort while library index build is in progress");
        return;
    }
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

/* ── UI selected-row state ───────────────────────────────────────────────── *
 *
 * The firmware and PC simulator share these production-named helpers. They
 * track the currently selected library row only; actual deck loads continue to
 * use media_catalog identity plus generation checks.
 */
void library_set_selected_track_index(int track_index)
{
    if (ensure_library_mutex() != ESP_OK) return;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (track_index >= 0 && track_index < s_track_count) {
        s_ui_track_idx = track_index;
    }
    xSemaphoreGiveRecursive(s_library_mutex);
}

int library_selected_track_index(void)
{
    if (ensure_library_mutex() != ESP_OK) return 0;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    int idx = s_ui_track_idx;
    xSemaphoreGiveRecursive(s_library_mutex);
    return idx;
}
