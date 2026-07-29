#include "audio_recorder.h"
#include "audio_recorder_ring.h"
#include "audio_recorder_sink.h"
#include "audio_recorder_wav.h"   /* AUDIO_RECORDER_WAV_FRAME_BYTES */
#include "audio_recorder_stop_gate.h"
#include "sd_io_gate.h"
#include "service_log.h"

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
static audio_recorder_stop_gate_t s_producer_gate;

/* Consumer-owned accumulators (writer task only). */
static uint32_t  s_push_count = 0u;
static uint32_t  s_push_max_us = 0u;
static uint32_t  s_push_over_100us = 0u;
/* microSD write cost, writer-task owned. */
static uint32_t  s_write_max_us = 0u;
static uint32_t  s_writes_over_100ms = 0u;
static uint32_t  s_reported_drops = 0u;
/* Stall-burst coalescing. Stalls arrive in runs of a dozen or more; one journal
 * record per run keeps the timeline without feeding card traffic back into the
 * stall it is describing. */
#define STALL_BURST_QUIET_US   2000000   /* run considered over after this gap */
#define DROP_REPORT_MIN_GAP_US 2000000
static uint32_t s_stall_burst_count = 0u;
static uint32_t s_stall_burst_worst_us = 0u;
static uint32_t s_stall_burst_ring = 0u;
static int64_t  s_stall_burst_started_us = 0;
static int64_t  s_last_stall_us = 0;
static int64_t  s_last_drop_report_us = 0;
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

/* Emit the pending stall-burst summary, if any. Called when the card has been
 * quiet long enough that the run is over, and once more at stop() so a burst
 * that ends the session is still reported. */
static void flush_stall_burst(void)
{
    if (s_stall_burst_count == 0u) {
        return;
    }
    uint32_t span_ms = (uint32_t)((s_last_stall_us - s_stall_burst_started_us) / 1000);
    service_log_event(SERVICE_LOG_RECORDING_SD_STALL, SERVICE_LOG_WARN,
                      4u, s_stall_burst_worst_us, s_stall_burst_count,
                      s_stall_burst_ring, span_ms, "worst/count/ring/span_ms");
    s_stall_burst_count = 0u;
    s_stall_burst_worst_us = 0u;
    s_stall_burst_ring = 0u;
}

/* The writer never frees the ring, finalizes the sink or sets STOPPED —
 * audio_recorder_stop() owns that after joining on WRITER_EXITED_BIT. On a fault
 * it sets ERROR; on a clean drain it leaves STOPPING for stop() to finalize. */
static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* A full ring means the card stalled longer than the buffer covers. That
         * is a lost stretch of audio, not a reason to end the session: killing
         * it here turned one stall into a finalize-and-reopen cycle whose own
         * microSD work then stalled playback again, and a 25 min soak lost two
         * recordings that way. Drop, account, and keep going — the WAV loses the
         * dropped span, so the file ends up shorter than the wall-clock take. */
        if (__atomic_exchange_n(&s_overflow, false, __ATOMIC_ACQ_REL)) {
            uint32_t lost = s_ring.dropped_blocks;
            int64_t now_us = esp_timer_get_time();
            if (lost != s_reported_drops &&
                (s_last_drop_report_us == 0 ||
                 now_us - s_last_drop_report_us >= DROP_REPORT_MIN_GAP_US)) {
                /* Same reasoning as the stall burst: during an overrun this
                 * fired on every writer iteration, adding card traffic to a
                 * card that was already the reason for the overrun. */
                service_log_event(SERVICE_LOG_RECORDING_DROPPED, SERVICE_LOG_WARN,
                                  3u, lost - s_reported_drops, lost,
                                  s_write_max_us, 0u, "ring full; card stalled");
                s_reported_drops = lost;
                s_last_drop_report_us = now_us;
            }
        }

        audio_recorder_state_t st = load_state();
        if (st != AUDIO_RECORDER_RECORDING && st != AUDIO_RECORDER_STOPPING) {
            break;
        }

        const audio_recorder_block_t *blk = audio_recorder_ring_peek(&s_ring);
        if (blk) {
            /* Time the card. The ring exists to cover write stalls, so knowing
             * how long a single block write actually blocks is what tells us
             * whether the buffer is undersized or the card is simply too slow. */
            int64_t w0 = esp_timer_get_time();
            esp_err_t rc = audio_recorder_sink_write_block(
                &s_sink, blk->samples, blk->frames, blk->sample_rate);
            uint32_t w_us = (uint32_t)(esp_timer_get_time() - w0);
            if (w_us > s_write_max_us) {
                s_write_max_us = w_us;
            }
            if (w_us >= 100000u) {
                s_writes_over_100ms++;
                /* Report at most one record per burst. Journalling every stall
                 * fed a loop back into the very thing being measured: each
                 * record goes to the journal writer, which writes it to the
                 * same card under the same gate, so a burst of fifteen stalls
                 * produced fifteen extra card transactions exactly when the
                 * card was already behind. Measured effect was gate_wait
                 * jumping from 8 ms to 185 ms the moment stalls began.
                 *
                 * The per-stall detail is not lost: the burst summary carries
                 * the worst stall and how many were folded into it, and the
                 * live counters stay exact on GET /api/recording. */
                if (s_stall_burst_worst_us == 0u) {
                    s_stall_burst_started_us = esp_timer_get_time();
                }
                s_stall_burst_count++;
                if (w_us > s_stall_burst_worst_us) {
                    s_stall_burst_worst_us = w_us;
                }
                s_stall_burst_ring = audio_recorder_ring_used(&s_ring);
            }
            s_last_stall_us = (w_us >= 100000u) ? esp_timer_get_time() : s_last_stall_us;
            if (s_stall_burst_count > 0u && w_us < 100000u &&
                esp_timer_get_time() - s_last_stall_us >= STALL_BURST_QUIET_US) {
                flush_stall_burst();
            }
            if (rc != ESP_OK) {
                s_last_error = rc;
                audio_recorder_stop_gate_close(&s_producer_gate);
                store_state(AUDIO_RECORDER_ERROR);
                service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                                  2u, (uint32_t)rc,
                                  (uint32_t)(s_frames_written / 1000u), 0u, 0u,
                                  "segment write failed");
                break;
            }
            s_bytes_written += (uint64_t)blk->frames * AUDIO_RECORDER_WAV_FRAME_BYTES;
            s_frames_written += blk->frames;
            audio_recorder_ring_consume(&s_ring);

            int64_t now = esp_timer_get_time();
            if (now - s_last_checkpoint_us >= AUDIO_RECORDER_CHECKPOINT_US) {
                esp_err_t checkpoint_rc = audio_recorder_sink_checkpoint(&s_sink);
                if (checkpoint_rc != ESP_OK) {
                    s_last_error = checkpoint_rc;
                    audio_recorder_stop_gate_close(&s_producer_gate);
                    store_state(AUDIO_RECORDER_ERROR);
                    service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                                      2u, (uint32_t)checkpoint_rc,
                                      (uint32_t)(s_frames_written / 1000u), 0u, 0u,
                                      "checkpoint failed");
                    break;
                }
                s_last_checkpoint_us = now;
                uint64_t freeb = 0u;
                if (audio_recorder_sink_free_bytes(&freeb) == ESP_OK &&
                    freeb < AUDIO_RECORDER_STOP_RESERVE_BYTES) {
                    ESP_LOGW(TAG, "microSD reserve reached (%llu B); stopping REC",
                             (unsigned long long)freeb);
                    s_last_error = ESP_ERR_NO_MEM;
                    audio_recorder_stop_gate_close(&s_producer_gate);
                    store_state(AUDIO_RECORDER_ERROR);
                    service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                                      2u, (uint32_t)ESP_ERR_NO_MEM,
                                      (uint32_t)(freeb >> 20), 0u, 0u,
                                      "microSD reserve reached");
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
    audio_recorder_stop_gate_init(&s_producer_gate);
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

#if CONFIG_AUDIO_RECORDER_RING_INTERNAL
    const size_t ring_budget = CONFIG_AUDIO_RECORDER_RING_INTERNAL_BYTES;
    const uint32_t ring_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const char *ring_where = "internal";
#else
    const size_t ring_budget = CONFIG_AUDIO_RECORDER_RING_BYTES;
    const uint32_t ring_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const char *ring_where = "PSRAM";
#endif

    uint32_t capacity = (uint32_t)(ring_budget / sizeof(audio_recorder_block_t));
    if (capacity < 2u) {
        capacity = 2u;
    }
    size_t bytes = (size_t)capacity * sizeof(audio_recorder_block_t);

    store_state(AUDIO_RECORDER_STARTING);

    /* Allocate the complete ring up front, behind the free check. */
    s_slots = heap_caps_malloc(bytes, ring_caps);
    if (!s_slots) {
        ESP_LOGE(TAG, "%s ring alloc %u B failed; recording not started",
                 ring_where, (unsigned)bytes);
        store_state(AUDIO_RECORDER_STOPPED);
        service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                          2u, (uint32_t)ESP_ERR_NO_MEM, (uint32_t)bytes, 0u, 0u,
                          "ring alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* Verify /sd, ensure the recordings directory and gate on free space. */
    uint64_t free_bytes = 0u;
    esp_err_t prep = audio_recorder_sink_prepare(&free_bytes);
    if (prep != ESP_OK) {
        ESP_LOGE(TAG, "microSD not ready for recording (%d)", (int)prep);
        release_ring();
        store_state(AUDIO_RECORDER_STOPPED);
        service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                          1u, (uint32_t)prep, 0u, 0u, 0u, "microSD not ready");
        return prep;
    }
    if (free_bytes < AUDIO_RECORDER_MIN_FREE_BYTES) {
        ESP_LOGE(TAG, "microSD low on space: %llu B free", (unsigned long long)free_bytes);
        release_ring();
        store_state(AUDIO_RECORDER_STOPPED);
        service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                          2u, (uint32_t)ESP_ERR_NO_MEM, (uint32_t)(free_bytes >> 20),
                          0u, 0u, "microSD low on space");
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
        service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                          2u, (uint32_t)orc, s_session, 0u, 0u,
                          "segment open failed");
        return orc;
    }

    audio_recorder_ring_init(&s_ring, s_slots, capacity);
    s_capacity = capacity;
    s_sample_rate = sample_rate;
    s_bytes_written = 0u;
    s_frames_written = 0u;
    s_push_count = 0u;
    s_push_max_us = 0u;
    s_push_over_100us = 0u;
    s_write_max_us = 0u;
    s_writes_over_100ms = 0u;
    s_reported_drops = 0u;
    s_stall_burst_count = 0u;
    s_stall_burst_worst_us = 0u;
    s_stall_burst_ring = 0u;
    s_stall_burst_started_us = 0;
    s_last_stall_us = 0;
    s_last_drop_report_us = 0;
    s_last_error = ESP_OK;
    s_last_checkpoint_us = esp_timer_get_time();
    __atomic_store_n(&s_overflow, false, __ATOMIC_RELEASE);

    xEventGroupClearBits(s_writer_exited, WRITER_EXITED_BIT);
    if (!audio_recorder_stop_gate_open(&s_producer_gate)) {
        audio_recorder_sink_abort(&s_sink);
        release_ring();
        store_state(AUDIO_RECORDER_STOPPED);
        return ESP_ERR_INVALID_STATE;
    }
    store_state(AUDIO_RECORDER_RECORDING);

    if (xTaskCreate(writer_task, "rec_writer", AUDIO_RECORDER_WRITER_STACK,
                    NULL, AUDIO_RECORDER_WRITER_PRIO, &s_writer) != pdPASS) {
        ESP_LOGE(TAG, "writer task create failed");
        audio_recorder_stop_gate_close(&s_producer_gate);
        store_state(AUDIO_RECORDER_STOPPED);
        (void)audio_recorder_sink_abort(&s_sink);
        release_ring();
        service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                          1u, (uint32_t)ESP_ERR_NO_MEM, 0u, 0u, 0u,
                          "writer task create failed");
        return ESP_ERR_NO_MEM;
    }

    sd_io_gate_set_recorder_active(true);
    service_log_event(SERVICE_LOG_RECORDING_STARTED, SERVICE_LOG_INFO,
                      4u, sample_rate, capacity, (uint32_t)(free_bytes >> 20),
                      s_session, NULL);
    ESP_LOGI(TAG, "recording started @ %u Hz, %s ring %u slots (%u B), %llu MiB free",
             (unsigned)sample_rate, ring_where, (unsigned)capacity, (unsigned)bytes,
             (unsigned long long)(free_bytes >> 20));
    return ESP_OK;
}

esp_err_t audio_recorder_stop(void)
{
    audio_recorder_state_t st = load_state();
    if (st == AUDIO_RECORDER_STOPPED && !s_slots) {
        audio_recorder_stop_gate_close(&s_producer_gate);
        return ESP_OK;
    }

    /* STOP is a three-stage ownership barrier:
     * 1. close producer admission;
     * 2. wait for a producer that passed the old RECORDING check to leave;
     * 3. publish STOPPING so the writer drains the now-closed ring to empty.
     * This prevents the old empty-ring race where one final master block could
     * arrive after the writer had already exited. */
    audio_recorder_stop_gate_close(&s_producer_gate);
    while (!audio_recorder_stop_gate_is_quiescent(&s_producer_gate)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (st == AUDIO_RECORDER_RECORDING || st == AUDIO_RECORDER_STARTING) {
        store_state(AUDIO_RECORDER_STOPPING);
    }
    if (s_writer) {
        xEventGroupWaitBits(s_writer_exited, WRITER_EXITED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);
        s_writer = NULL;
    }

    uint32_t rec_seconds = s_sample_rate
        ? (uint32_t)(s_frames_written / s_sample_rate) : 0u;
    uint32_t rec_drops = s_ring.dropped_blocks;

    /* A faulted session is never promoted to an ordinary .wav. It is durably
     * closed as `.part`, and boot recovery may later expose it explicitly as a
     * `.recovered.wav`. A clean session is renamed only when patch, fsync and
     * close all succeed. */
    bool session_ok = s_last_error == ESP_OK && load_state() != AUDIO_RECORDER_ERROR;
    esp_err_t close_rc = session_ok
        ? audio_recorder_sink_finalize(&s_sink)
        : audio_recorder_sink_abort(&s_sink);
    if (close_rc != ESP_OK && s_last_error == ESP_OK) {
        s_last_error = close_rc;
        service_log_event(SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_ERROR,
                          2u, (uint32_t)close_rc,
                          (uint32_t)(s_frames_written / 1000u), 0u, 0u,
                          "finalize failed; .part retained");
    }

    release_ring();
    store_state(AUDIO_RECORDER_STOPPED);
    flush_stall_burst();
    sd_io_gate_set_recorder_active(false);
    service_log_event(SERVICE_LOG_RECORDING_STOPPED,
                      (s_last_error == ESP_OK) ? SERVICE_LOG_INFO : SERVICE_LOG_WARN,
                      4u, rec_seconds, rec_drops, s_push_over_100us,
                      s_push_max_us, NULL);
    ESP_LOGI(TAG, "recording stopped (bytes=%llu err=%d)",
             (unsigned long long)s_bytes_written, (int)s_last_error);
    return s_last_error;
}

bool audio_recorder_push_master(const int16_t *stereo, size_t frames,
                                uint32_t sample_rate)
{
    if (load_state() != AUDIO_RECORDER_RECORDING ||
        !audio_recorder_stop_gate_try_enter(&s_producer_gate)) {
        return false;
    }
    int64_t t0 = esp_timer_get_time();
    bool ok = audio_recorder_ring_push(&s_ring, stereo, (uint32_t)frames, sample_rate);
    audio_recorder_stop_gate_leave(&s_producer_gate);
    uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - t0);

    /* Producer-owned counters: only the audio output task updates them. */
    s_push_count++;
    if (elapsed_us > s_push_max_us) {
        s_push_max_us = elapsed_us;
    }
    if (elapsed_us >= 100u) {
        s_push_over_100us++;
    }

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
    out->push_count = s_push_count;
    out->push_max_us = s_push_max_us;
    out->push_over_100us = s_push_over_100us;
    out->write_max_us = s_write_max_us;
    out->writes_over_100ms = s_writes_over_100ms;
    audio_recorder_sink_write_cost(&out->gate_wait_max_us, &out->fwrite_max_us);
    out->last_error = s_last_error;
    return ESP_OK;
}

audio_recorder_state_t audio_recorder_get_state(void)
{
    return load_state();
}
