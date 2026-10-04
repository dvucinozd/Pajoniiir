#include "ui_artwork.h"
#include "ui_artwork_thumb.h"
#include "ui_artwork_jpeg.h"

#include "library.h"
#include "media_io_gate.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "ui_artwork";

#define UI_ARTWORK_QUEUE      16
#define UI_ARTWORK_FILE_MAX   (64u * 1024u)   /* rekordbox aN.jpg is 2-8 KiB */
#define UI_ARTWORK_CHUNK      4096u           /* media_io_gate hold per read */
#define UI_ARTWORK_STACK      8192            /* PSRAM, like ui_load */
#define UI_ARTWORK_PRIO       1               /* below ui_load (3) and LVGL (4) */
#define UI_ARTWORK_CORE       1               /* audio tasks run on core 0 */
#define UI_ARTWORK_PAUSE_MS   50
#define UI_ARTWORK_GAP_MS     125             /* <= 8 reads+decodes/s: a page per second */

typedef enum { SLOT_EMPTY, SLOT_PENDING, SLOT_READY, SLOT_NONE } slot_state_t;
typedef enum { RESULT_SKIPPED, RESULT_READY, RESULT_NONE } result_t;

typedef struct {
    uint32_t key, gen, epoch;
} art_req_t;

typedef struct {
    uint32_t key, gen, stamp;
    uint8_t state;
} art_slot_t;

/* LVGL task only. */
static art_slot_t s_slot[UI_ARTWORK_SLOTS];
static ui_artwork_thumb_t *s_thumbs;          /* UI_ARTWORK_SLOTS, PSRAM */
static uint32_t s_clock, s_gen;
static bool s_retry, s_init_done, s_init_failed;
static ui_artwork_stats_t s_stats;

/* Worker -> LVGL handoff: one staged result, released by ui_artwork_poll(). */
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_stage_free;
static ui_artwork_thumb_t *s_stage;
static art_req_t s_stage_req;
static result_t s_stage_result;
static uint32_t s_stage_read_us, s_stage_decode_us;
static atomic_bool s_stage_ready;
static atomic_uint s_epoch;
static atomic_bool s_paused;

/* Worker only. */
static uint8_t *s_file;
static ui_artwork_thumb_work_t *s_work;
static TickType_t s_last_read;

static void *art_alloc(size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool art_stale(const art_req_t *req)
{
    return req->epoch != atomic_load(&s_epoch) ||
           req->gen != library_generation();
}

static bool art_gate_begin(void)
{
    media_io_gate_begin();
    return media_io_gate_is_available();
}

static void art_gate_end(void)
{
    media_io_gate_end();
}

/* Whole file, bounded gate intervals like the PDB reader. */
static size_t art_read_file(const char *path)
{
    bool available = art_gate_begin();
    FILE *fp = available ? fopen(path, "rb") : NULL;
    long size = fp && fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
    art_gate_end();
    if (!fp) return 0;
    size_t len = size > 0 && (size_t)size <= UI_ARTWORK_FILE_MAX ? (size_t)size : 0;
    for (size_t off = 0; off < len;) {
        size_t n = len - off > UI_ARTWORK_CHUNK ? UI_ARTWORK_CHUNK : len - off;
        available = art_gate_begin();
        bool ok = available &&
                  fseek(fp, (long)off, SEEK_SET) == 0 &&
                  fread(s_file + off, 1, n, fp) == n;
        art_gate_end();
        if (!ok) {
            len = 0;
            break;
        }
        off += n;
    }
    (void)art_gate_begin();
    fclose(fp);
    art_gate_end();
    return len;
}

/* Spaced reads: the stick is shared with the decks' audio streaming, and a
 * burst of covers only has to fill a page, not win a race. */
static void art_pace(void)
{
    TickType_t gap = pdMS_TO_TICKS(UI_ARTWORK_GAP_MS);
    TickType_t since = xTaskGetTickCount() - s_last_read;
    if (since < gap) vTaskDelay(gap - since);
}

/* The stats time the read (USB gate waits included) and the decode apart,
 * the pacing and the track-load pause excluded. */
static result_t art_decode(const art_req_t *req)
{
    art_pace();
    while (atomic_load(&s_paused)) {
        if (art_stale(req)) return RESULT_SKIPPED;
        vTaskDelay(pdMS_TO_TICKS(UI_ARTWORK_PAUSE_MS));
    }
    if (art_stale(req)) return RESULT_SKIPPED;
    char export_path[PDB_ARTWORK_PATH_MAX];
    if (library_artwork_path_for_key(req->key, req->gen, export_path,
                                     sizeof(export_path)) != ESP_OK)
        return RESULT_NONE;
    char path[LIBRARY_PATH_MAX];
    if (snprintf(path, sizeof(path), "/usb%s", export_path) >= (int)sizeof(path))
        return RESULT_NONE;
    int64_t t0 = esp_timer_get_time();
    size_t len = art_read_file(path);
    int64_t t1 = esp_timer_get_time();
    s_last_read = xTaskGetTickCount();
    s_stage_read_us = (uint32_t)(t1 - t0);
    if (len == 0) {
        ESP_LOGW(TAG, "track %u: cannot read %.48s", (unsigned)req->key, path);
        return RESULT_NONE;
    }
    if (art_stale(req)) return RESULT_SKIPPED;
    ui_artwork_jpeg_info_t header = ui_artwork_jpeg_probe(s_file, len);
    if (header.verdict != UI_ARTWORK_JPEG_OK ||
        header.width == 0u || header.height == 0u ||
        header.width > UI_ARTWORK_SRC_MAX ||
        header.height > UI_ARTWORK_SRC_MAX)
        return RESULT_NONE;
    bool ok = ui_artwork_thumb_decode(s_file, len, s_work, s_stage);
    s_stage_decode_us = (uint32_t)(esp_timer_get_time() - t1);
    if (!ok) {
        ui_artwork_jpeg_info_t why = ui_artwork_jpeg_probe(s_file, len);
        ESP_LOGW(TAG, "track %u: JPEG not decodable (%u B, %ux%u SOF%02X, %u comp, "
                 "sampling %02X/%02X/%02X: %s)",
                 (unsigned)req->key, (unsigned)len, (unsigned)why.width, (unsigned)why.height,
                 (unsigned)why.sof, (unsigned)why.components, (unsigned)why.sampling[0],
                 (unsigned)why.sampling[1], (unsigned)why.sampling[2],
                 ui_artwork_jpeg_verdict_name(why.verdict));
        return RESULT_NONE;
    }
    return RESULT_READY;
}

static void art_worker(void *arg)
{
    (void)arg;
    art_req_t req;
    for (;;) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        xSemaphoreTake(s_stage_free, portMAX_DELAY);
        s_stage_read_us = s_stage_decode_us = 0;
        s_stage_result = art_stale(&req) ? RESULT_SKIPPED : art_decode(&req);
        s_stage_req = req;
        atomic_store(&s_stage_ready, true);
    }
}

static bool art_init(void)
{
    if (s_init_done) return !s_init_failed;
    s_init_done = true;
    s_thumbs = art_alloc(UI_ARTWORK_SLOTS * sizeof(ui_artwork_thumb_t));
    s_stage = art_alloc(sizeof(ui_artwork_thumb_t));
    s_file = art_alloc(UI_ARTWORK_FILE_MAX);
    s_work = art_alloc(sizeof(ui_artwork_thumb_work_t));
    s_queue = xQueueCreate(UI_ARTWORK_QUEUE, sizeof(art_req_t));
    s_stage_free = xSemaphoreCreateBinary();
    if (s_thumbs && s_stage && s_file && s_work && s_queue && s_stage_free) {
        xSemaphoreGive(s_stage_free);
        if (xTaskCreatePinnedToCoreWithCaps(art_worker, "ui_art", UI_ARTWORK_STACK, NULL,
                                            UI_ARTWORK_PRIO, NULL, UI_ARTWORK_CORE,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
            ESP_LOGI(TAG, "cache ready: %d slots, %u KiB PSRAM", UI_ARTWORK_SLOTS,
                     (unsigned)((UI_ARTWORK_SLOTS + 1u) * sizeof(ui_artwork_thumb_t) +
                                UI_ARTWORK_FILE_MAX + sizeof(ui_artwork_thumb_work_t)) / 1024u);
            return true;
        }
    }
    ESP_LOGE(TAG, "no memory for the artwork cache: covers disabled");
    heap_caps_free(s_thumbs);
    heap_caps_free(s_stage);
    heap_caps_free(s_file);
    heap_caps_free(s_work);
    if (s_queue) vQueueDelete(s_queue);
    if (s_stage_free) vSemaphoreDelete(s_stage_free);
    s_thumbs = s_stage = NULL;
    s_file = NULL;
    s_work = NULL;
    s_queue = NULL;
    s_stage_free = NULL;
    s_init_failed = true;
    return false;
}

static const uint16_t *slot_pixels(int i, ui_artwork_size_t size)
{
    return size == UI_ARTWORK_DECK ? s_thumbs[i].deck : s_thumbs[i].row;
}

/* Empty or stale first, then the least recently used finished entry.
 * Pending entries of this generation are never taken. */
static int slot_victim(void)
{
    int best = -1;
    for (int i = 0; i < UI_ARTWORK_SLOTS; i++) {
        const art_slot_t *s = &s_slot[i];
        if (s->state == SLOT_EMPTY || s->gen != s_gen) return i;
        if (s->state == SLOT_PENDING) continue;
        if (best < 0 || (int32_t)(s->stamp - s_slot[best].stamp) < 0) best = i;
    }
    return best;
}

const uint16_t *ui_artwork_get(uint32_t track_key, ui_artwork_size_t size)
{
    if (track_key == 0u || !art_init()) return NULL;
    s_gen = library_generation();
    for (int i = 0; i < UI_ARTWORK_SLOTS; i++) {
        art_slot_t *s = &s_slot[i];
        if (s->state == SLOT_EMPTY || s->key != track_key || s->gen != s_gen) continue;
        s->stamp = ++s_clock;
        return s->state == SLOT_READY ? slot_pixels(i, size) : NULL;
    }
    int v = slot_victim();
    art_req_t req = {
        .key = track_key,
        .gen = s_gen,
        .epoch = atomic_load(&s_epoch),
    };
    if (v < 0 || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        s_retry = true;
        s_stats.queue_full++;
        return NULL;
    }
    s_stats.queued++;
    s_slot[v] = (art_slot_t){ .key = track_key, .gen = s_gen, .stamp = ++s_clock, .state = SLOT_PENDING };
    return NULL;
}

void ui_artwork_begin_page(void)
{
    atomic_fetch_add(&s_epoch, 1u);
}

bool ui_artwork_poll(void)
{
    if (!s_init_done || s_init_failed) return false;
    int64_t start_us = esp_timer_get_time();
    s_gen = library_generation();
    bool changed = false;
    if (atomic_load(&s_stage_ready)) {
        if (s_stage_req.epoch != atomic_load(&s_epoch) ||
            s_stage_req.gen != s_gen) s_stage_result = RESULT_SKIPPED;
        if (s_stage_result == RESULT_READY) s_stats.decoded++;
        else if (s_stage_result == RESULT_NONE) s_stats.none++;
        else s_stats.skipped++;
        if (s_stage_read_us > s_stats.read_us_max) s_stats.read_us_max = s_stage_read_us;
        if (s_stage_decode_us > s_stats.decode_us_max) s_stats.decode_us_max = s_stage_decode_us;
        for (int i = 0; i < UI_ARTWORK_SLOTS; i++) {
            art_slot_t *s = &s_slot[i];
            if (s->state != SLOT_PENDING || s->key != s_stage_req.key || s->gen != s_stage_req.gen) continue;
            if (s_stage_result == RESULT_READY) {
                memcpy(&s_thumbs[i], s_stage, sizeof(ui_artwork_thumb_t));
                s->state = SLOT_READY;
            } else {
                /* A skipped row may be back on screen: re-ask. */
                s->state = s_stage_result == RESULT_NONE ? SLOT_NONE : SLOT_EMPTY;
            }
            changed = true;
            break;
        }
        atomic_store(&s_stage_ready, false);
        xSemaphoreGive(s_stage_free);
    }
    if (s_retry && uxQueueSpacesAvailable(s_queue) > 0) {
        s_retry = false;
        changed = true;
    }
    uint32_t poll_us = (uint32_t)(esp_timer_get_time() - start_us);
    if (poll_us > s_stats.poll_us_max) s_stats.poll_us_max = poll_us;
    return changed;
}

void ui_artwork_forget(uint32_t track_key)
{
    for (int i = 0; i < UI_ARTWORK_SLOTS; i++) {
        art_slot_t *s = &s_slot[i];
        /* A pending decode lands on its slot: it reads the file again. */
        if (s->key == track_key && s->state != SLOT_PENDING) s->state = SLOT_EMPTY;
    }
}

void ui_artwork_set_paused(bool paused)
{
    atomic_store(&s_paused, paused);
}

void ui_artwork_take_stats(ui_artwork_stats_t *out)
{
    if (out) *out = s_stats;
    s_stats = (ui_artwork_stats_t){0};
}
