#include "audio_recorder.h"
#include "audio_recorder_ring.h"
#include "audio_recorder_sink.h"
#include "audio_recorder_wav.h"   /* AUDIO_RECORDER_WAV_FRAME_BYTES */
#include "sd_io_gate.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <string.h>

#include "sdkconfig.h"

static const char *TAG = "audio_recorder";

#ifndef CONFIG_AUDIO_RECORDER_RING_BYTES
#define CONFIG_AUDIO_RECORDER_RING_BYTES 524288   /* 512 KiB default */
#endif

#define AUDIO_RECORDER_WRITER_STACK   4096
#define AUDIO_RECORDER_WRITER_PRIO    3          /* below audio/decode and LVGL */
#define AUDIO_RECORDER_IDLE_MS        10u        /* poll cadence when ring empty */
#define AUDIO_RECORDER_MIN_FREE_BYTES (128ull * 1024ull * 1024ull)  /* to start */
#define AUDIO_RECORDER_STOP_RESERVE_BYTES (64ull * 1024ull * 1024ull) /* stop at */
#define AUDIO_RECORDER_CHECKPOINT_US  (10ll * 1000000ll)            /* 10 s */

#define WRITER_EXITED_BIT (1u << 0)

/* State holds audio_recorder_state_t values, published with release/acquire so
 * push_master() observes a coherent transition without a lock on the hot path. */
static int s_state = AUDIO_RECORDER_STOPPED;

static audio_recorder_ring_t   s_ring;
static audio_recorder_block_t *s_slots = NULL;
static uint32_t                s_capacity = 0u;
static uint32_t                s_sample_rate = 0u;

static audio_recorder_sink_t s_sink;
static uint32_t              s_boot_id = 0u;
static uint32_t              s_session = 0u;
static int64_t               s_last_checkpoint_us = 0;

static TaskHandle_t       s_writer = NULL;   /* marker only; never dereferenced after exit */
static EventGroupHandle_t s_writer_exited = NULL;
static bool               s_overflow = false;

/* Consumer-owned accumulators (writer task only). */
static uint64_t  s_bytes_written = 0u;
static uint64_t  s_frames_written = 0u;
static esp_err_t s_last_error = ESP_OK;

static inline audio_recorder_state_t load_state(void)
{
    return (audio_recorder_state_t)__atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
}

static inline void store_state(audio_recorder_state_t st)
{
    __atomic_store_n(&s_state, (int)st, __ATOMIC_RELEASE);
}

/* Monotonic, NVS-persisted boot id so segment names are unique and ordered
 * across reboots. Falls back to esp_random() when NVS is unavailable. */
static uint32_t load_persistent_boot_id(void)
{
    nvs_handle_t h;
    uint32_t id = 0u;
    if (nvs_open("audio_rec", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_get_u32(h, "boot_id", &id);
        id++;
        if (id == 0u) {
            id = 1u;
        }
        if (nvs_set_u32(h, "boot_id", id) == ESP_OK) {
            (void)nvs_commit(h);
        }
        nvs_close(h);
    }
    if (id == 0u) {
        id = esp_random() | 1u;
    }
    return id;
}

/* Free the ring and reset the stopped bookkeeping. */
static void release_ring(void)
{
    if (s_slots) {
        heap_caps_free(s_slots);
        s_slots = NULL;
    }
    s_capacity = 0u;
    s_sample_rate = 0u;
}

/* The writer never frees the ring, finalizes the sink or sets STOPPED —
 * audio_recorder_stop() owns that after joining on WRITER_EXITED_BIT. On a fault
 * it sets ERROR; on a clean drain it leaves STOPPING for stop() to finalize. */
static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (__atomic_load_n(&s_overflow, __ATOMIC_ACQUIRE)) {
            if (s_last_error == ESP_OK) {
                s_last_error = ESP_ERR_NO_MEM;
            }
            store_state(AUDIO_RECORDER_ERROR);
            break;
        }

        audio_recorder_state_t st = load_state();
        if (st != AUDIO_RECORDER_RECORDING && st != AUDIO_RECORDER_STOPPING) {
            break;
        }

        const audio_recorder_block_t *blk = audio_recorder_ring_peek(&s_ring);
        if (blk) {
            esp_err_t rc = audio_recorder_sink_write_block(
                &s_sink, blk->samples, blk->frames, blk->sample_rate);
            if (rc != ESP_OK) {
                s_last_error = rc;
                store_state(AUDIO_RECORDER_ERROR);
                break;
            }
            s_bytes_written += (uint64_t)blk->frames * AUDIO_RECORDER_WAV_FRAME_BYTES;
            s_frames_written += blk->frames;
            audio_recorder_ring_consume(&s_ring);

            int64_t now = esp_timer_get_time();
            if (now - s_last_checkpoint_us >= AUDIO_RECORDER_CHECKPOINT_US) {
                audio_recorder_sink_checkpoint(&s_sink);
                s_last_checkpoint_us = now;
                uint64_t freeb = 0u;
                if (audio_recorder_sink_free_bytes(&freeb) == ESP_OK &&
                    freeb < AUDIO_RECORDER_STOP_RESERVE_BYTES) {
                    ESP_LOGW(TAG, "microSD reserve reached (%llu B); stopping REC",
                             (unsigned long long)freeb);
                    s_last_error = ESP_ERR_NO_MEM;
                    store_state(AUDIO_RECORDER_ERROR);
                    break;
                }
            }
            continue;
        }

        if (st == AUDIO_RECORDER_STOPPING) {
            break;   /* ring empty and stopping -> done; stop() finalizes */
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_RECORDER_IDLE_MS));
    }

    xEventGroupSetBits(s_writer_exited, WRITER_EXITED_BIT);
    vTaskDelete(NULL);
}

esp_err_t audio_recorder_init(void)
{
    if (!s_writer_exited) {
        s_writer_exited = xEventGroupCreate();
        if (!s_writer_exited) {
            return ESP_ERR_NO_MEM;
        }
        if (s_boot_id == 0u) {
            s_boot_id = load_persistent_boot_id();
        }
        /* Recover any .part left by a crash/power loss before new recording. */
        audio_recorder_sink_recover_orphans();
    }
    store_state(AUDIO_RECORDER_STOPPED);
    return ESP_OK;
}

esp_err_t audio_recorder_start(uint32_t sample_rate)
{
    if (!s_writer_exited) {
        esp_err_t rc = audio_recorder_init();
        if (rc != ESP_OK) {
            return rc;
        }
    }
    if (load_state() != AUDIO_RECORDER_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate == 0u) {
        return ESP_ERR_INVALID_ARG;   /* an output rate must exist first */
    }

    uint32_t capacity = (uint32_t)(CONFIG_AUDIO_RECORDER_RING_BYTES /
                                   sizeof(audio_recorder_block_t));
    if (capacity < 2u) {
        capacity = 2u;
    }
    size_t bytes = (size_t)capacity * sizeof(audio_recorder_block_t);

    store_state(AUDIO_RECORDER_STARTING);

    /* Allocate the complete ring up front, in PSRAM, behind the free check. */
    s_slots = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_slots) {
        ESP_LOGE(TAG, "PSRAM ring alloc %u B failed; recording not started",
                 (unsigned)bytes);
        store_state(AUDIO_RECORDER_STOPPED);
        return ESP_ERR_NO_MEM;
    }

    /* Verify /sd, ensure the recordings directory and gate on free space. */
    uint64_t free_bytes = 0u;
    esp_err_t prep = audio_recorder_sink_prepare(&free_bytes);
    if (prep != ESP_OK) {
        ESP_LOGE(TAG, "microSD not ready for recording (%d)", (int)prep);
        release_ring();
        store_state(AUDIO_RECORDER_STOPPED);
        return prep;
    }
    if (free_bytes < AUDIO_RECORDER_MIN_FREE_BYTES) {
        ESP_LOGE(TAG, "microSD low on space: %llu B free", (unsigned long long)free_bytes);
        release_ring();
        store_state(AUDIO_RECORDER_STOPPED);
        return ESP_ERR_NO_MEM;
    }

    if (s_boot_id == 0u) {
        s_boot_id = esp_random() | 1u;   /* nonzero session-unique prefix */
    }
    s_session++;
    esp_err_t orc = audio_recorder_sink_open(&s_sink, sample_rate, s_boot_id, s_session);
    if (orc != ESP_OK) {
        ESP_LOGE(TAG, "recording segment open failed (%d)", (int)orc);
        release_ring();
        store_state(AUDIO_RECORDER_STOPPED);
        return orc;
    }

    audio_recorder_ring_init(&s_ring, s_slots, capacity);
    s_capacity = capacity;
    s_sample_rate = sample_rate;
    s_bytes_written = 0u;
    s_frames_written = 0u;
    s_last_error = ESP_OK;
    s_last_checkpoint_us = esp_timer_get_time();
    __atomic_store_n(&s_overflow, false, __ATOMIC_RELEASE);

    xEventGroupClearBits(s_writer_exited, WRITER_EXITED_BIT);
    store_state(AUDIO_RECORDER_RECORDING);

    if (xTaskCreate(writer_task, "rec_writer", AUDIO_RECORDER_WRITER_STACK,
                    NULL, AUDIO_RECORDER_WRITER_PRIO, &s_writer) != pdPASS) {
        ESP_LOGE(TAG, "writer task create failed");
        store_state(AUDIO_RECORDER_STOPPED);
        audio_recorder_sink_finalize(&s_sink);
        release_ring();
        return ESP_ERR_NO_MEM;
    }

    sd_io_gate_set_recorder_active(true);
    ESP_LOGI(TAG, "recording started @ %u Hz, ring %u slots (%u B), %llu MiB free",
             (unsigned)sample_rate, (unsigned)capacity, (unsigned)bytes,
             (unsigned long long)(free_bytes >> 20));
    return ESP_OK;
}

esp_err_t audio_recorder_stop(void)
{
    audio_recorder_state_t st = load_state();
    if (st == AUDIO_RECORDER_STOPPED && !s_slots) {
        return ESP_OK;
    }

    /* Ask the writer to finish draining; a faulted writer has already exited. */
    if (st == AUDIO_RECORDER_RECORDING || st == AUDIO_RECORDER_STARTING) {
        store_state(AUDIO_RECORDER_STOPPING);
    }
    if (s_writer) {
        /* Join only via the event group; never touch the handle after exit. */
        xEventGroupWaitBits(s_writer_exited, WRITER_EXITED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);
        s_writer = NULL;
    }

    /* Finalize whatever was written (bounded salvage on an ERROR session). */
    audio_recorder_sink_finalize(&s_sink);
    release_ring();
    store_state(AUDIO_RECORDER_STOPPED);
    sd_io_gate_set_recorder_active(false);
    ESP_LOGI(TAG, "recording stopped (bytes=%llu err=%d)",
             (unsigned long long)s_bytes_written, (int)s_last_error);
    return ESP_OK;
}

bool audio_recorder_push_master(const int16_t *stereo, size_t frames,
                                uint32_t sample_rate)
{
    if (load_state() != AUDIO_RECORDER_RECORDING) {
        return false;
    }
    bool ok = audio_recorder_ring_push(&s_ring, stereo, (uint32_t)frames, sample_rate);
    if (!ok) {
        __atomic_store_n(&s_overflow, true, __ATOMIC_RELEASE);
    }
    return ok;
}

esp_err_t audio_recorder_get_status(audio_recorder_status_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->state = load_state();
    out->sample_rate = s_sample_rate;
    out->ring_capacity = s_capacity;
    out->ring_used = s_slots ? audio_recorder_ring_used(&s_ring) : 0u;
    out->ring_high_water = s_ring.high_water;
    out->dropped_blocks = s_ring.dropped_blocks;
    out->dropped_frames = s_ring.dropped_frames;
    out->bytes_written = s_bytes_written;
    out->frames_written = s_frames_written;
    out->last_error = s_last_error;
    return ESP_OK;
}

audio_recorder_state_t audio_recorder_get_state(void)
{
    return load_state();
}
