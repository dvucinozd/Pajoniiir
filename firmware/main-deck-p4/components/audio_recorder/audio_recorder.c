#include "audio_recorder.h"
#include "audio_recorder_ring.h"
#include "audio_recorder_writer.h"
#include "audio_recorder_wav.h"   /* AUDIO_RECORDER_WAV_FRAME_BYTES */
#include "sd_io_gate.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
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
#define AUDIO_RECORDER_DRAIN_BATCH    16u        /* blocks per writer pass */
#define AUDIO_RECORDER_IDLE_MS        10u        /* poll cadence when ring empty */

#define WRITER_EXITED_BIT (1u << 0)

/* State holds audio_recorder_state_t values, published with release/acquire so
 * push_master() observes a coherent transition without a lock on the hot path.
 * A plain int (not the enum, not volatile) keeps __atomic well-defined. */
static int s_state = AUDIO_RECORDER_STOPPED;

static audio_recorder_ring_t   s_ring;
static audio_recorder_block_t *s_slots = NULL;
static uint32_t                s_capacity = 0u;
static uint32_t                s_sample_rate = 0u;

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

/* Placeholder storage sink. Real microSD WAV segment writes replace this in a
 * later slice; for now it validates the drain path and accounts bytes. */
static int counting_sink(void *ctx, const void *data, size_t len)
{
    (void)ctx;
    (void)data;
    return (int)len;
}

/* The writer never frees the ring or sets STOPPED — audio_recorder_stop() owns
 * that after joining on WRITER_EXITED_BIT. On a fault it sets ERROR; on a clean
 * drain it leaves STOPPING for stop() to finalize. */
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

        audio_recorder_drain_result_t res = audio_recorder_writer_drain(
            &s_ring, counting_sink, NULL, AUDIO_RECORDER_DRAIN_BATCH);
        s_bytes_written += res.bytes_written;
        s_frames_written += res.bytes_written / AUDIO_RECORDER_WAV_FRAME_BYTES;
        if (res.sink_error) {
            s_last_error = ESP_FAIL;
            store_state(AUDIO_RECORDER_ERROR);
            break;
        }
        if (st == AUDIO_RECORDER_STOPPING &&
            audio_recorder_ring_used(&s_ring) == 0u) {
            break;   /* fully drained; stop() finalizes */
        }
        if (res.blocks_written > 0u) {
            continue;   /* keep draining while data is flowing */
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
        return ESP_ERR_INVALID_ARG;
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
    audio_recorder_ring_init(&s_ring, s_slots, capacity);
    s_capacity = capacity;
    s_sample_rate = sample_rate;
    s_bytes_written = 0u;
    s_frames_written = 0u;
    s_last_error = ESP_OK;
    __atomic_store_n(&s_overflow, false, __ATOMIC_RELEASE);

    xEventGroupClearBits(s_writer_exited, WRITER_EXITED_BIT);
    store_state(AUDIO_RECORDER_RECORDING);

    if (xTaskCreate(writer_task, "rec_writer", AUDIO_RECORDER_WRITER_STACK,
                    NULL, AUDIO_RECORDER_WRITER_PRIO, &s_writer) != pdPASS) {
        ESP_LOGE(TAG, "writer task create failed");
        store_state(AUDIO_RECORDER_STOPPED);
        heap_caps_free(s_slots);
        s_slots = NULL;
        s_capacity = 0u;
        s_sample_rate = 0u;
        return ESP_ERR_NO_MEM;
    }

    /* Make the shared SD arbiter defer heavy optional admin work while REC. */
    sd_io_gate_set_recorder_active(true);

    ESP_LOGI(TAG, "recording started @ %u Hz, ring %u slots (%u B)",
             (unsigned)sample_rate, (unsigned)capacity, (unsigned)bytes);
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

    if (s_slots) {
        heap_caps_free(s_slots);
        s_slots = NULL;
    }
    s_capacity = 0u;
    s_sample_rate = 0u;
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
