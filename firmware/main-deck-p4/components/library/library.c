#include "library.h"
#include "rekordbox_pdb.h"
#include "rekordbox_anlz.h"
#include "track_meta_cache.h"
#include "library_load_trace.h"
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

/* Host suites compile this source without sdkconfig.h and exercise the product
 * default. Firmware receives the Kconfig value from ESP-IDF. */
#if defined(WIN32) && !defined(CONFIG_LIBRARY_ANLZ_CACHE_WRITE)
#define CONFIG_LIBRARY_ANLZ_CACHE_WRITE 1
#endif

/* Referenced only by the ESP_LOG* macros, which the PC host stubs compile away.
 * Marking it used keeps -Wall clean there without an #ifdef around every log. */
__attribute__((unused)) static const char *TAG = "library";

/* USB drive mount point — set by USB host VFS when the drive is mounted */
#ifndef WIN32
#define USB_MOUNT_POINT  "/usb"
#else
#define USB_MOUNT_POINT  "C:/Users/klikn/Music/USB"
#endif
#define USB_PDB_PATH     USB_MOUNT_POINT "/PIONEER/rekordbox/export.pdb"

/* Track records are built transactionally into the inactive PSRAM buffer and
 * become immutable once published. Sorting never copies or moves these ~2.9 KiB
 * records; it republishes only a double-buffered uint16_t row-order table.
 * The second record buffer remains necessary so a slow USB rebuild cannot
 * disturb readers of the currently published catalog.
 *
 * Record buffers are sized to the catalog actually being published, not to
 * LIBRARY_MAX_TRACKS: two maximum-size buffers are ~6 MiB of PSRAM held for the
 * lifetime of the process even for a ten-track stick. Only the rebuild window
 * pays for two buffers; the superseded one is released as soon as the swap has
 * happened under the lock, so steady state holds exactly one. */
#define LIBRARY_MAX_TRACKS  1024

typedef uint16_t library_order_entry_t;
_Static_assert(LIBRARY_MAX_TRACKS <= UINT16_MAX,
               "library order entries must address every track slot");

static library_track_t *s_track_buf[2] = { NULL, NULL };
static int              s_track_cap[2] = { 0, 0 };
static library_order_entry_t *s_order_buf[2] = { NULL, NULL };
static int              s_active_buf = 0;
static int              s_active_order_buf = 0;
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

/* Order tables are 2 KiB each at the maximum track count, so they stay fixed-size
 * and preallocated; only the multi-megabyte record buffers are demand-sized. */
static esp_err_t ensure_index_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        if (!s_order_buf[i]) {
            s_order_buf[i] = heap_caps_malloc(
                LIBRARY_MAX_TRACKS * sizeof(library_order_entry_t),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!s_order_buf[i]) {
                s_order_buf[i] = malloc(
                    LIBRARY_MAX_TRACKS * sizeof(library_order_entry_t));
            }
            if (!s_order_buf[i]) {
                ESP_LOGE(TAG, "Out of memory for row-order buffer %d", i);
                return ESP_ERR_NO_MEM;
            }
        }
    }
    return ESP_OK;
}

/* Give buffer slot `buf` room for `count` records, reusing it when it is already
 * large enough. A rebuild that shrinks the catalog keeps the larger allocation
 * rather than churning multi-megabyte PSRAM blocks; the superseded slot is
 * released after publish, which is what actually bounds steady-state usage. */
static esp_err_t reserve_track_buffer(int buf, int count)
{
    if (count <= 0) count = 1;
    if (s_track_buf[buf] && s_track_cap[buf] >= count) {
        return ESP_OK;
    }

    const size_t bytes = (size_t)count * sizeof(library_track_t);
    library_track_t *fresh = heap_caps_malloc(bytes,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fresh) {
        ESP_LOGW(TAG, "SPIRAM unavailable for track buffer %d, using internal heap", buf);
        fresh = malloc(bytes);
    }
    if (!fresh) {
        ESP_LOGE(TAG, "Out of memory for %d track records in buffer %d", count, buf);
        return ESP_ERR_NO_MEM;
    }

    free(s_track_buf[buf]);
    s_track_buf[buf] = fresh;
    s_track_cap[buf] = count;
    return ESP_OK;
}

/* Caller holds s_library_mutex and has already published the other slot. Every
 * reader copies under that same lock, so nothing can still be inside this buffer. */
static void release_track_buffer_locked(int buf)
{
    if (!s_track_buf[buf]) return;
    free(s_track_buf[buf]);
    s_track_buf[buf] = NULL;
    s_track_cap[buf] = 0;
}

static inline library_track_t *active_tracks(void)
{
    return s_track_buf[s_active_buf];
}

static inline library_order_entry_t *active_order(void)
{
    return s_order_buf[s_active_order_buf];
}

/* Caller holds s_library_mutex. Logical rows always resolve through the current
 * order table; the underlying published track slots never move during sort. */
static int library_slot_for_row_unlocked(int row)
{
    library_order_entry_t *order = active_order();
    if (!order || row < 0 || row >= s_track_count) {
        return -1;
    }
    int slot = (int)order[row];
    return slot < s_track_count ? slot : -1;
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
    /* The PDB/audio duration covers the whole file including any outro after the
     * final beat, so the beatgrid is only a fallback for a catalog row that has
     * no duration at all. Overwriting unconditionally used to truncate playback
     * length to the last beat, and was being undone again by two separate callers
     * downstream; enforce it once, here, where the value is produced. */
    if (track->duration_ms == 0u && meta->beat_count > 0 && meta->beats) {
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

    int build_order_buf = s_active_order_buf ^ 1;
    library_order_entry_t *build_order = s_order_buf[build_order_buf];
    int build_count = 0;

    /* media_io_gate serialises every USB reader, and the audio decode path takes
     * it on each compressed-cache miss. Holding it across the whole catalog walk
     * therefore blocked playback for the entire parse - up to LIBRARY_MAX_TRACKS
     * reads - so loading the library stalled both decks. Take the gate per USB
     * operation instead: the parse gets slightly more gate traffic, and audio
     * gets to interleave.
     *
     * Releasing between rows also makes an unmount observable mid-parse rather
     * than something the parse holds straight through, which the loop below
     * checks for. */
    pdb_t *pdb = NULL;
    media_io_gate_begin();
    rc = pdb_open(USB_PDB_PATH, &pdb);
    int n = (rc == ESP_OK) ? pdb_track_count(pdb) : 0;
    media_io_gate_end();
    if (rc != ESP_OK) {
        xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
        s_index_building = false;
        xSemaphoreGiveRecursive(s_library_mutex);
        ESP_LOGW(TAG, "PDB not found at %s (USB not mounted?)", USB_PDB_PATH);
        return rc;
    }

    if (n > LIBRARY_MAX_TRACKS) n = LIBRARY_MAX_TRACKS;

    /* Size the record buffer now that the real track count is known, rather than
     * reserving LIBRARY_MAX_TRACKS up front. */
    rc = reserve_track_buffer(build_buf, n);
    if (rc != ESP_OK) {
        media_io_gate_begin();
        pdb_close(pdb);
        media_io_gate_end();
        xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
        s_index_building = false;
        xSemaphoreGiveRecursive(s_library_mutex);
        return rc;
    }
    library_track_t *build_index = s_track_buf[build_buf];
    memset(build_index, 0, (size_t)s_track_cap[build_buf] * sizeof(library_track_t));

    for (int i = 0; i < n; i++) {
        pdb_track_t pt;
        media_io_gate_begin();
        esp_err_t row_rc = pdb_get_track(pdb, i, &pt);
        media_io_gate_end();

        /* The drive can go away between rows now that the gate is released
         * there. Check before acting on row_rc, not after: an unmount makes
         * every remaining read fail, and `continue` would then walk the whole
         * rest of the catalog against a dead mount. Stop instead - the rows
         * gathered so far are still published, which is what the old code
         * produced for a mid-parse read failure anyway. */
        if (!media_io_gate_is_available()) {
            ESP_LOGW(TAG, "media went away %d/%d rows into the catalog walk", i, n);
            break;
        }
        if (row_rc != ESP_OK) continue;

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

    media_io_gate_begin();
    pdb_close(pdb);
    media_io_gate_end();

    for (int i = 0; i < build_count; ++i) {
        build_order[i] = (library_order_entry_t)i;
    }

    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (s_generation != build_generation) {
        /* library_clear() ran while the media was being parsed (for example,
         * because the drive was removed).  Never republish that stale index. */
        s_index_building = false;
        xSemaphoreGiveRecursive(s_library_mutex);
        ESP_LOGW(TAG, "Discarding stale library index build");
        return ESP_ERR_INVALID_STATE;
    }
    const int superseded_buf = s_active_buf;
    s_active_buf = build_buf;
    s_active_order_buf = build_order_buf;
    s_track_count = build_count;
    if (s_ui_track_idx >= build_count) {
        s_ui_track_idx = 0;
    }
    s_generation++;
    s_index_building = false;
    /* The previously published records are now unreachable: every reader resolves
     * through active_tracks() while holding this same lock. Releasing here is what
     * keeps steady-state usage at one record buffer instead of two. */
    if (superseded_buf != build_buf) {
        release_track_buffer_locked(superseded_buf);
    }
    xSemaphoreGiveRecursive(s_library_mutex);

    ESP_LOGI(TAG, "Library ready: %d tracks from PDB (%u KiB of records)",
             build_count,
             (unsigned)(((size_t)build_count * sizeof(library_track_t)) / 1024u));
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
    library_track_t *idx = active_tracks();
    int slot = library_slot_for_row_unlocked(index);
    if (!idx || slot < 0) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *out = idx[slot];
    xSemaphoreGiveRecursive(s_library_mutex);
    return ESP_OK;
}

/* ── library_get_ptr (simulator only) ─────────────────────────────────────── *
 *
 * Returns a pointer into the immutable published track store for the requested
 * logical row. Sorting cannot invalidate it because only the order table flips;
 * a later library_init() rebuild may still publish the other track buffer. The
 * single-threaded PC simulator can live with that lifetime, while firmware code
 * must use library_get()/library_get_summary() (or media_catalog).
 */
#ifdef WIN32
library_track_t *library_get_ptr(int index)
{
    if (ensure_library_mutex() != ESP_OK) return NULL;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_tracks();
    int slot = library_slot_for_row_unlocked(index);
    library_track_t *track = (!idx || slot < 0) ? NULL : &idx[slot];
    xSemaphoreGiveRecursive(s_library_mutex);
    return track;
}
#endif

/* ── library_get_summary ──────────────────────────────────────────────────── */

esp_err_t library_get_summary(int index, uint16_t *out_bpm, uint32_t *out_duration_ms)
{
    if (ensure_library_mutex() != ESP_OK) return ESP_ERR_NOT_FOUND;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_tracks();
    int slot = library_slot_for_row_unlocked(index);
    if (!idx || slot < 0) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    if (out_bpm) *out_bpm = idx[slot].bpm;
    if (out_duration_ms) *out_duration_ms = idx[slot].duration_ms;
    xSemaphoreGiveRecursive(s_library_mutex);
    return ESP_OK;
}

/* ── library_get_row_key / library_find_row_by_key ────────────────────────── *
 *
 * Identity-only accessors. library_get() copies a whole library_track_t (~2.9 KB,
 * dominated by waveform_low[400] and pvbr[400]) under the library mutex, which is
 * pure waste when the caller only wants to know which track a row holds — and it
 * was being paid per LVGL draw task and per element of several linear scans.
 * These read the two identity fields in place instead.
 */
esp_err_t library_get_row_key(int index, uint32_t *out_key)
{
    if (!out_key) return ESP_ERR_INVALID_ARG;
    *out_key = 0u;
    if (ensure_library_mutex() != ESP_OK) return ESP_ERR_NOT_FOUND;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_tracks();
    int slot = library_slot_for_row_unlocked(index);
    esp_err_t rc = ESP_ERR_NOT_FOUND;
    if (idx && slot >= 0) {
        *out_key = library_track_key(&idx[slot]);
        rc = ESP_OK;
    }
    xSemaphoreGiveRecursive(s_library_mutex);
    return rc;
}

int library_find_row_by_key(uint32_t track_key)
{
    if (track_key == 0u) return -1;
    if (ensure_library_mutex() != ESP_OK) return -1;
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    library_track_t *idx = active_tracks();
    int found = -1;
    if (idx) {
        for (int row = 0; row < s_track_count; ++row) {
            int slot = library_slot_for_row_unlocked(row);
            if (slot >= 0 && library_track_key(&idx[slot]) == track_key) {
                found = row;
                break;
            }
        }
    }
    xSemaphoreGiveRecursive(s_library_mutex);
    return found;
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
    library_load_trace_mark(LIBRARY_LOAD_PHASE_CACHE_USB_STAT, track_key);
    esp_err_t cache_rc = track_meta_cache_load(track_key, dat_path, ext_path, true, out);
    if (cache_rc == ESP_OK) {
        if (source) *source = LIBRARY_ANLZ_SRC_CACHE;
        return ESP_OK;
    }

    /* Cold path: parse DAT once and EXT once, then one best-effort cache write.
     * track_meta_cache_load already zeroed *out on its miss return. */
    media_io_gate_begin();
    library_load_trace_mark(LIBRARY_LOAD_PHASE_USB_DAT, track_key);
    esp_err_t rc = anlz_parse_dat(dat_path, out);
    if (rc != ESP_OK) {
        media_io_gate_end();
        ESP_LOGE(TAG, "anlz_parse_dat failed: %s", dat_path);
        return rc;
    }
    library_load_trace_mark(LIBRARY_LOAD_PHASE_USB_EXT, track_key);
    anlz_parse_ext(ext_path, out);           /* best-effort high-res waveform */
    media_io_gate_end();

#if CONFIG_LIBRARY_ANLZ_CACHE_WRITE
    library_load_trace_mark(LIBRARY_LOAD_PHASE_CACHE_SAVE_USB_STAT, track_key);
    esp_err_t save_rc = track_meta_cache_save(track_key, dat_path, ext_path, out);
    if (cache_written) *cache_written = (save_rc == ESP_OK);
#else
    /* Optional acceleration only. On the experimental P4 power setup, the
     * larger cache commit was hardware-proven to brown out during fclose()/
     * atomic replacement. Parsing remains successful and the owned metadata
     * continues through the normal publish path. */
    if (cache_written) *cache_written = false;
#endif
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
    library_load_trace_boot_init();
    ESP_RETURN_ON_ERROR(ensure_library_mutex(), TAG, "library mutex");

    int64_t t_start = esp_timer_get_time();

    anlz_metadata_t meta;
    uint32_t track_key = library_track_key(track);
    library_load_trace_mark(LIBRARY_LOAD_PHASE_RESOLVE, track_key);
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
            library_load_trace_mark(LIBRARY_LOAD_PHASE_FREE_OLD, track_key);
            anlz_free(&stale_meta);
        }
        library_load_trace_mark(LIBRARY_LOAD_PHASE_FAILED, track_key);
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
    library_load_trace_mark(LIBRARY_LOAD_PHASE_PUBLISH_LOCK, track_key);
    xSemaphoreTakeRecursive(s_library_mutex, portMAX_DELAY);
    if (s_current_meta_valid) {
        old_meta = s_current_meta;
        have_old = true;
    }
    s_current_meta = meta;
    s_current_meta_valid = true;
    xSemaphoreGiveRecursive(s_library_mutex);
    if (have_old) {
        library_load_trace_mark(LIBRARY_LOAD_PHASE_FREE_OLD, track_key);
        anlz_free(&old_meta);
    }

    int64_t elapsed_us = esp_timer_get_time() - t_start;
    const char *src_name = (source == LIBRARY_ANLZ_SRC_CACHE) ? "cache" : "usb";
    (void)src_name; /* only read by ESP_LOGI, which is a no-op in the host build */
    __atomic_store_n(&s_last_load_elapsed_ms, (uint32_t)(elapsed_us / 1000), __ATOMIC_RELAXED);
    __atomic_store_n(&s_last_load_source, (uint32_t)source, __ATOMIC_RELAXED);
    __atomic_store_n(&s_last_load_cache_written, cache_written ? 1u : 0u, __ATOMIC_RELAXED);
    ESP_LOGI(TAG, "ANLZ \"%s\" src=%s bpm=%u dur=%lums cues=%u beats=%u hi-wav=%u %lldus",
             track->title[0] ? track->title : track->path, src_name,
             track->bpm, (unsigned long)track->duration_ms,
             meta.cue_count, meta.beat_count, meta.waveform_high_len,
             (long long)elapsed_us);
    library_load_trace_mark(LIBRARY_LOAD_PHASE_DONE, track_key);
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

/* qsort() has no portable context parameter. library_sort() holds the library
 * mutex for the complete synchronous sort, so this comparator context cannot
 * race with another sort or a track-store publication. */
static const library_track_t *s_sort_tracks;
static int (*s_sort_track_compare)(const void *, const void *);

static int compare_order_entries(const void *a, const void *b)
{
    library_order_entry_t slot_a = *(const library_order_entry_t *)a;
    library_order_entry_t slot_b = *(const library_order_entry_t *)b;
    int cmp = s_sort_track_compare(&s_sort_tracks[slot_a],
                                   &s_sort_tracks[slot_b]);
    if (cmp == 0) {
        /* Deterministic tie-breaker: retain original PDB slot order even though
         * the C library qsort itself is not stable. */
        return (int)slot_a - (int)slot_b;
    }
    return cmp;
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

    library_track_t *tracks = active_tracks();
    library_order_entry_t *source_order = active_order();
    if (!tracks || !source_order || s_track_count <= 1) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return;
    }

    if (field_type == 0) { // Artist
        s_sort_track_compare = descending ? compare_artist_desc : compare_artist_asc;
    } else if (field_type == 1) { // Title / Name
        s_sort_track_compare = descending ? compare_title_desc : compare_title_asc;
    } else if (field_type == 2) { // BPM
        s_sort_track_compare = descending ? compare_bpm_desc : compare_bpm_asc;
    } else if (field_type == 3) { // Key
        s_sort_track_compare = descending ? compare_key_desc : compare_key_asc;
    } else {
        xSemaphoreGiveRecursive(s_library_mutex);
        ESP_LOGW(TAG, "Ignoring unsupported library sort field %d", field_type);
        return;
    }

    /* Publish-on-write now copies only the compact row order (2 KiB at the
     * 1024-track maximum), not ~2.9 MiB of library_track_t records. */
    int build_order_buf = s_active_order_buf ^ 1;
    library_order_entry_t *order = s_order_buf[build_order_buf];
    if (!order) {
        xSemaphoreGiveRecursive(s_library_mutex);
        return;
    }
    memcpy(order, source_order,
           (size_t)s_track_count * sizeof(library_order_entry_t));

    s_sort_tracks = tracks;
    qsort(order, (size_t)s_track_count, sizeof(library_order_entry_t),
          compare_order_entries);
    s_sort_tracks = NULL;
    s_sort_track_compare = NULL;

    s_active_order_buf = build_order_buf;
    s_generation++;
    xSemaphoreGiveRecursive(s_library_mutex);
    ESP_LOGI(TAG, "Library order sorted: field=%d, descending=%d", field_type, descending);
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
