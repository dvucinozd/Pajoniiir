/*
 * audio_engine.c — MP3 decode + PCM output
 *
 * Decodes MP3 with minimp3 (single-header, public-domain).
 *
 * Platform (compile-time define):
 *   AUDIO_ENGINE_PC_TEST       → WAV file output (audio_engine_decode_to_wav)
 *   (neither)                  → firmware: ES8311/I2S real-time output
 */

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "audio_engine.h"
#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"
#include "audio_fw_task_context.h"
#include "audio_fw_task_plan.h"
#include "audio_mixer.h"
#include "audio_output_mixer.h"
#include "audio_pcm_ring.h"
#include "audio_resampler.h"
#if !defined(AUDIO_ENGINE_PC_TEST)
#include "media_io_gate.h"
#endif

/* ESP-IDF logging — stubbed out in PC test builds */
#if defined(AUDIO_ENGINE_PC_TEST)
#   include <stdio.h>
#   define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#   define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#   define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#   define ESP_LOGD(tag, fmt, ...) ((void)0)
#else
#   include "esp_log.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#if AE_PC
#   include <time.h>
#endif

static const char *TAG = "audio";

/* ── Platform selection ───────────────────────────────────────────────────── */
#if defined(AUDIO_ENGINE_PC_TEST)
#   include <pthread.h>
#   define AE_PC  1
#else
#   define AE_PC  0
#endif

#define AE_SDL 0

/* Firmware (ESP32-P4): real-time I2S output through the ES8311 codec */
#if !AE_PC
#   define AE_FW 1
#   include "freertos/FreeRTOS.h"
#   include "freertos/task.h"
#   include "freertos/semphr.h"
#   include "freertos/idf_additions.h"
#   include "esp_heap_caps.h"
#   include "esp_timer.h"
#   include "bsp_jc4880.h"
#   include "esp_codec_dev.h"
#else
#   define AE_FW 0
#endif

static int64_t ae_now_us(void)
{
#if AE_FW
    return esp_timer_get_time();
#elif AE_PC
    return (int64_t)clock() * 1000000LL / (int64_t)CLOCKS_PER_SEC;
#else
    return 0;
#endif
}

/* ── PCM ring buffers (stereo int16 PCM frames) ───────────────────────────── *
 *
 * Producer: decode thread (PC) / decode task (firmware).
 * Consumer: SDL audio callback (PC simulator) or codec/I2S output task (firmware).
 */
static audio_pcm_ring_t   s_pcm_rings[AUDIO_ENGINE_DECK_COUNT];

/* Shared scratchpad buffer for decoding to avoid stack allocation */
static int16_t            s_scratch_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2u];

static void reset_all_pcm_rings(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_pcm_ring_reset(&s_pcm_rings[i]);
    }
}

static bool deck_is_valid(uint8_t deck);

/* ── Engine state ─────────────────────────────────────────────────────────── */
typedef struct {
    FILE    *fp;
    mp3dec_t dec;

    /* Direct memory-mapped buffer for firmware (bypasses fmemopen bugs) */
    const uint8_t *file_buf;
    size_t         file_size;
    size_t         file_pos;

    /* PVBR seek table — 400 file-byte offsets (from ANLZ0000.DAT) */
    uint32_t pvbr[AUDIO_PVBR_LEN];
    bool     has_pvbr;
    uint32_t duration_ms;

    /* Detected from first decoded frame */
    uint32_t sample_rate;
    int      channels;

    /* Decode cursor: frames decoded since the last seek. */
    uint32_t seek_base_ms;
    uint64_t frames_since_seek;

    /* Playback cursor: source frames consumed by the output resampler since the
     * last seek. This is what the UI/deck should expose as audible position. */
    uint32_t output_base_ms;
    uint64_t output_frames_since_seek;

    /* Pitch: 1.0 = ±0%, > 1.0 = faster, < 1.0 = slower  (range 0.9 – 1.1) */
    float    pitch_factor;

    bool     loaded;
    bool     playing;
    bool     paused;
    bool     eof;
    bool     loading;
    uint8_t  load_progress;
    esp_err_t last_error;
    char     last_error_text[64];

    /* Asynchronous seek */
    volatile uint32_t seek_target_ms;
    volatile bool     seek_requested;
    /* Loop-wrap seek: reposition the decoder to loop_start but DO NOT flush the
     * ring — the not-yet-played audio (up to loop_end) must play out first, so
     * the loop is gapless and keeps its full length. User seeks flush as usual. */
    volatile bool     seek_is_loop;

    /* Real-time loop */
    volatile uint32_t loop_start_ms;
    volatile uint32_t loop_end_ms;
    volatile bool     loop_active;

    /* Instant Frame-Index Seek */
    uint32_t *seek_table;
    uint32_t  seek_table_len;
} audio_engine_state_t;

static audio_engine_state_t  s_engines[AUDIO_ENGINE_DECK_COUNT];
static audio_engine_state_t *s_active_eng = &s_engines[AUDIO_ENGINE_COMPAT_DECK];

#define s_eng (*s_active_eng)

static inline audio_pcm_ring_t *active_pcm_ring(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        if (&s_engines[i] == s_active_eng) {
            return &s_pcm_rings[i];
        }
    }
    return &s_pcm_rings[AUDIO_ENGINE_COMPAT_DECK];
}

static inline audio_pcm_ring_t *pcm_ring_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_pcm_rings[deck];
    }
    return &s_pcm_rings[AUDIO_ENGINE_COMPAT_DECK];
}

static inline uint8_t active_deck_index(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        if (&s_engines[i] == s_active_eng) {
            return i;
        }
    }
    return AUDIO_ENGINE_COMPAT_DECK;
}

static inline uint32_t ring_free(void) { return audio_pcm_ring_free(active_pcm_ring()); }

static inline void ring_reset(void) { audio_pcm_ring_reset(active_pcm_ring()); }

static inline bool ring_push(int16_t l, int16_t r)
{
    return audio_pcm_ring_push(active_pcm_ring(), l, r);
}

static uint16_t         s_channel_volume[AUDIO_ENGINE_DECK_COUNT] = {
    AUDIO_MIXER_CONTROL_MAX,
    AUDIO_MIXER_CONTROL_MAX,
};
static uint16_t         s_crossfader = AUDIO_MIXER_CONTROL_CENTER;
static bool             s_pfl_enabled[AUDIO_ENGINE_DECK_COUNT];

/* ── Mutex + decode thread ────────────────────────────────────────────────── *
 * Mutex: present in all PC builds (no-op in single-threaded PC_TEST).
 * Decode thread: only in PC_SIMULATOR (SDL consumer runs in separate thread).
 */
#if AE_PC
static pthread_mutex_t    s_file_mutex  = PTHREAD_MUTEX_INITIALIZER;
#   define AE_LOCK()   pthread_mutex_lock(&s_file_mutex)
#   define AE_UNLOCK() pthread_mutex_unlock(&s_file_mutex)
#elif AE_FW
static SemaphoreHandle_t  s_file_mutex  = NULL;   /* created in audio_engine_init */
#   define AE_LOCK()   do { if (s_file_mutex) xSemaphoreTakeRecursive(s_file_mutex, portMAX_DELAY); } while (0)
#   define AE_UNLOCK() do { if (s_file_mutex) xSemaphoreGiveRecursive(s_file_mutex); } while (0)
#else
#   define AE_LOCK()   do {} while (0)
#   define AE_UNLOCK() do {} while (0)
#endif



#if AE_FW
static esp_codec_dev_handle_t s_codec       = NULL;  /* owned by bsp_jc4880 */
static SemaphoreHandle_t      s_tasks_done  = NULL;  /* counting sem: each task gives on exit */
static SemaphoreHandle_t      s_output_done = NULL;
static TaskHandle_t           s_output_task = NULL;
static volatile bool          s_output_run = false;
static volatile bool          s_output_codec_open = false;
static uint32_t               s_output_sample_rate = 0;
/* The MP3 is preloaded into PSRAM once and decoded directly from the
 * memory buffer. This keeps USB off the playback/teardown path entirely — streaming
 * reads from /usb during playback collide with the load sequence and trip a
 * USB-DWC channel assert. The only USB access is one bulk read at load time. */
static audio_fw_preload_t     s_fw_preloads[AUDIO_ENGINE_DECK_COUNT];
static audio_fw_runtime_t     s_fw_runtimes[AUDIO_ENGINE_DECK_COUNT];
static audio_fw_task_context_t s_fw_task_contexts[AUDIO_ENGINE_DECK_COUNT];
#endif

/* ── Pitch-resampling state (firmware I2S output) ─────────────────────────── */
#if AE_FW
static audio_resampler_state_t s_resamplers[AUDIO_ENGINE_DECK_COUNT];

static audio_resampler_state_t *active_resampler(void)
{
    return &s_resamplers[active_deck_index()];
}

static audio_resampler_state_t *resampler_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_resamplers[deck];
    }
    return &s_resamplers[AUDIO_ENGINE_COMPAT_DECK];
}

static bool pop_ring_source(void *ctx, audio_mixer_frame_t *out_frame)
{
    return audio_pcm_ring_pop((audio_pcm_ring_t *)ctx, out_frame);
}

static audio_fw_preload_t *active_fw_preload(void)
{
    return &s_fw_preloads[active_deck_index()];
}

static audio_fw_runtime_t *active_fw_runtime(void)
{
    return &s_fw_runtimes[active_deck_index()];
}

static audio_fw_task_context_t *active_fw_task_context(void)
{
    return &s_fw_task_contexts[active_deck_index()];
}

static void reset_all_fw_preloads(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_fw_preload_reset(&s_fw_preloads[i]);
    }
}

static void reset_all_fw_runtimes(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_fw_runtime_reset(&s_fw_runtimes[i]);
    }
}

static void reset_all_fw_task_contexts(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_fw_task_context_reset(&s_fw_task_contexts[i]);
    }
}

static bool audio_fw_output_task_running(void)
{
    return s_output_run && s_output_task != NULL;
}

static bool deck_output_active(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return false;
    audio_engine_state_t *eng = &s_engines[deck];
    return eng->playing && !eng->paused;
}

static void update_deck_output_position(uint8_t deck, uint32_t consumed)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT || consumed == 0u) return;
    audio_engine_state_t *eng = &s_engines[deck];
    eng->output_frames_since_seek += consumed;
    if (eng->loop_active && eng->sample_rate > 0) {
        uint32_t played_ms = eng->output_base_ms +
            (uint32_t)(eng->output_frames_since_seek * 1000u / eng->sample_rate);
        if (played_ms >= eng->loop_end_ms) {
            eng->output_base_ms = eng->loop_start_ms;
            eng->output_frames_since_seek = 0u;
        }
    }
}

static void reset_all_resamplers(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_resampler_reset(&s_resamplers[i]);
    }
}

static bool any_deck_loaded(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        if (s_engines[i].loaded) {
            return true;
        }
    }
    return false;
}

#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═════════════════════════════════════════════════════════════════════════ */

/*
 * decode_one_frame — read + decode one MP3 frame from s_eng.fp.
 *
 * Returns PCM samples-per-channel (>0), or 0 on EOF / no frame found.
 * out_pcm[] is stereo-interleaved: out_pcm[i*2]=L, out_pcm[i*2+1]=R.
 * Upmixes mono → stereo in-place before returning.
 *
 * File position advances by exactly one frame (frame_bytes).
 * Caller must hold s_file_mutex if called from multiple threads.
 */
static int decode_one_frame(
    audio_engine_state_t *eng,
#if AE_FW
    audio_fw_preload_t *fw,
#endif
    int16_t out_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2])
{
    if (eng->file_buf) {
        size_t available = eng->file_size;
#if AE_FW
        if (!fw) return 0;
        if (!fw->load_done && fw->loaded_bytes < available) {
            available = fw->loaded_bytes;
        }
#endif

        if (eng->eof || eng->file_pos >= eng->file_size) {
            eng->eof = true;
            return 0;
        }
        if (eng->file_pos >= available) {
#if AE_FW
            if (!fw->load_done) {
                return 0;   /* loader has not reached this byte yet */
            }
#endif
            eng->eof = true;
            return 0;
        }

        size_t bytes_left = available - eng->file_pos;
        if (bytes_left == 0) {
#if AE_FW
            if (!fw->load_done) {
                return 0;
            }
#endif
            eng->eof = true;
            return 0;
        }
#if AE_FW
        if (!fw->load_done && bytes_left < 4096) {
            return 0;   /* avoid skipping bytes from a partially loaded frame */
        }
#endif

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&eng->dec, eng->file_buf + eng->file_pos, (int)bytes_left, s_scratch_pcm, &info);

        if (info.frame_bytes > 0) {
            eng->file_pos += (size_t)info.frame_bytes;
        } else {
#if AE_FW
            if (!fw->load_done) {
                return 0;
            }
#endif
            eng->file_pos += 1;
            return 0;
        }

        if (samples == 0) return 0; /* header-only frame */

        /* Latch sample rate and channels from first real audio frame */
        if (eng->sample_rate == 0 && info.hz > 0) {
            eng->sample_rate = (uint32_t)info.hz;
            eng->channels    = info.channels;
            ESP_LOGI(TAG, "MP3: %d Hz, %d ch, %d kbps", info.hz, info.channels, info.bitrate_kbps);
        }

        /* Copy to out_pcm, upmixing mono → stereo */
        if (info.channels == 1) {
            for (int i = samples - 1; i >= 0; i--) {
                out_pcm[i * 2 + 1] = s_scratch_pcm[i];
                out_pcm[i * 2    ] = s_scratch_pcm[i];
            }
        } else {
            memcpy(out_pcm, s_scratch_pcm, (size_t)(samples * 2) * sizeof(int16_t));
        }

        return samples;
    } else {
        if (!eng->fp || eng->eof) return 0;

        /* Read a window large enough to contain at least one MP3 frame.
         * Max CBR frame at 320 kbps ≈ 1441 bytes; 4096 is always sufficient. */
        uint8_t buf[4096];
        long    pos_before = ftell(eng->fp);
        size_t  bytes_read = fread(buf, 1u, sizeof buf, eng->fp);
        if (bytes_read == 0) { eng->eof = true; return 0; }

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&eng->dec, buf, (int)bytes_read, s_scratch_pcm, &info);

        /* Reposition file to just after the consumed frame (or +1 if no sync) */
        if (info.frame_bytes > 0) {
            fseek(eng->fp, pos_before + (long)info.frame_bytes, SEEK_SET);
        } else {
            /* No MP3 sync found — advance 1 byte to search further */
            fseek(eng->fp, pos_before + 1L, SEEK_SET);
            return 0;
        }

        if (samples == 0) return 0; /* header-only frame (Xing/VBRi/LAME) */

        /* Latch sample rate and channels from first real audio frame */
        if (eng->sample_rate == 0 && info.hz > 0) {
            eng->sample_rate = (uint32_t)info.hz;
            eng->channels    = info.channels;
            ESP_LOGI(TAG, "MP3: %d Hz, %d ch, %d kbps", info.hz, info.channels, info.bitrate_kbps);
        }

        /* Copy to out_pcm, upmixing mono → stereo */
        if (info.channels == 1) {
            /* Expand in-place from end to avoid overwriting unread source samples */
            for (int i = samples - 1; i >= 0; i--) {
                out_pcm[i * 2 + 1] = s_scratch_pcm[i];
                out_pcm[i * 2    ] = s_scratch_pcm[i];
            }
        } else {
            memcpy(out_pcm, s_scratch_pcm, (size_t)(samples * 2) * sizeof(int16_t));
        }

        return samples; /* samples per channel */
    }
}

/*
 * build_seek_table — fast frame scanning by reading headers in PSRAM/file.
 * Does NOT decode PCM audio to ensure sub-millisecond execution.
 */
static void build_seek_table(audio_engine_state_t *eng)
{
    if (!eng->file_buf && !eng->fp) return;

    uint32_t cap = 20000; /* initial capacity for ~8-10 mins track */
#if AE_FW
    eng->seek_table = heap_caps_malloc(cap * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
#else
    eng->seek_table = malloc(cap * sizeof(uint32_t));
#endif
    if (!eng->seek_table) {
        ESP_LOGE(TAG, "Failed to allocate seek table memory!");
        return;
    }
    eng->seek_table_len = 0;

    size_t pos = 0;
    size_t size = eng->file_size;
    uint8_t *scan_buf = NULL;

    /* For PC, read the file into a temporary buffer for fast memory scanning */
    if (!eng->file_buf && eng->fp) {
        long prev_pos = ftell(eng->fp);
        fseek(eng->fp, 0, SEEK_END);
        long fsz = ftell(eng->fp);
        fseek(eng->fp, 0, SEEK_SET);
        if (fsz > 0) {
            scan_buf = malloc(fsz);
            if (scan_buf) {
                size_t read_bytes = fread(scan_buf, 1, fsz, eng->fp);
                size = read_bytes;
            }
        }
        fseek(eng->fp, prev_pos, SEEK_SET);
    }

    const uint8_t *buf = eng->file_buf ? eng->file_buf : scan_buf;
    if (!buf) {
        if (scan_buf) free(scan_buf);
        return;
    }

    int64_t t0 = ae_now_us();

    while (pos + 4 <= size) {
        if (hdr_valid(buf + pos)) {
            const uint8_t *hdr = buf + pos;
            int frame_bytes = hdr_frame_bytes(hdr, 0) + hdr_padding(hdr);
            if (frame_bytes <= 0) {
                pos++;
                continue;
            }

            if (eng->seek_table_len >= cap) {
                cap *= 2;
#if AE_FW
                uint32_t *new_table = heap_caps_realloc(eng->seek_table, cap * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
#else
                uint32_t *new_table = realloc(eng->seek_table, cap * sizeof(uint32_t));
#endif
                if (!new_table) {
                    ESP_LOGE(TAG, "Failed to reallocate seek table!");
                    break;
                }
                eng->seek_table = new_table;
            }

            eng->seek_table[eng->seek_table_len++] = (uint32_t)pos;
            pos += frame_bytes;
        } else {
            pos++;
        }
    }

    if (scan_buf) {
        free(scan_buf);
    }

    int64_t dt_us = ae_now_us() - t0;
    ESP_LOGI(TAG, "Indexed %u MP3 frames in %lld ms", 
             (unsigned)eng->seek_table_len, (long long)(dt_us / 1000));
}

/*
 * seek_index — ultra-fast O(1) seek using our custom frame index seek table.
 * Caller holds s_file_mutex.
 */
static void seek_index(audio_engine_state_t *eng, uint32_t position_ms)
{
    if (!eng->seek_table || eng->seek_table_len == 0) return;

    uint32_t sr = (eng->sample_rate > 0) ? eng->sample_rate : 44100u;
    uint32_t samples_per_frame = 1152u;

    /* Read first frame header to obtain exact properties */
    const uint8_t *buf = eng->file_buf;
    if (buf && eng->seek_table_len > 0) {
        const uint8_t *hdr = buf + eng->seek_table[0];
        samples_per_frame = hdr_frame_samples(hdr);
        sr = hdr_sample_rate_hz(hdr);
    } else if (eng->fp && eng->seek_table_len > 0) {
        long prev = ftell(eng->fp);
        fseek(eng->fp, (long)eng->seek_table[0], SEEK_SET);
        uint8_t hdr[4];
        if (fread(hdr, 1, 4, eng->fp) == 4) {
            samples_per_frame = hdr_frame_samples(hdr);
            sr = hdr_sample_rate_hz(hdr);
        }
        fseek(eng->fp, prev, SEEK_SET);
    }

    double frame_idx_double = (double)position_ms * (double)sr / ((double)samples_per_frame * 1000.0);
    uint32_t target_frame = (uint32_t)(frame_idx_double + 0.5);

    if (target_frame >= eng->seek_table_len) {
        target_frame = eng->seek_table_len - 1;
    }

    uint32_t target_byte = eng->seek_table[target_frame];

    if (eng->file_buf) {
        eng->file_pos = target_byte;
    } else if (eng->fp) {
        fseek(eng->fp, (long)target_byte, SEEK_SET);
    }

    eng->seek_base_ms = (uint32_t)((double)target_frame * (double)samples_per_frame * 1000.0 / (double)sr);
    eng->frames_since_seek = 0;

    ESP_LOGI(TAG, "Index seek %u ms → frame %u/%u (byte %u), actual position: %u ms",
             (unsigned)position_ms, (unsigned)target_frame, (unsigned)eng->seek_table_len,
             (unsigned)target_byte, (unsigned)eng->seek_base_ms);
}

/*
 * seek_pvbr — fast O(1) seek using the 400-entry PVBR table.
 * Caller holds s_file_mutex.
 */
static void seek_pvbr(audio_engine_state_t *eng, uint32_t position_ms)
{
    uint32_t idx = (eng->duration_ms > 0)
                   ? (position_ms * AUDIO_PVBR_LEN / eng->duration_ms)
                   : 0u;
    if (idx >= AUDIO_PVBR_LEN) idx = AUDIO_PVBR_LEN - 1u;
    uint32_t target_byte = eng->pvbr[idx];

    if (eng->file_buf) {
        if (target_byte > eng->file_size) target_byte = eng->file_size;
        eng->file_pos = target_byte;
        ESP_LOGI(TAG, "PVBR seek %u ms → table[%u] = byte %u",
                 (unsigned)position_ms, (unsigned)idx, (unsigned)target_byte);
    } else {
        int rc = fseek(eng->fp, (long)target_byte, SEEK_SET);
        long actual_pos = ftell(eng->fp);
        ESP_LOGI(TAG, "PVBR seek %u ms → table[%u] = byte %u (fseek ret=%d, ftell=%ld)",
                 (unsigned)position_ms, (unsigned)idx, (unsigned)target_byte, rc, actual_pos);
    }
}

/*
 * seek_estimate — O(1) seek used when neither an IFI seek-table nor a usable
 * PVBR table is available. Estimates the byte offset assuming roughly constant
 * bitrate, sets the read cursor there, and lets minimp3 resync to the next frame
 * header. This replaces the old linear decode-scan from the file start, which
 * ran a tight non-yielding loop (starving CPU 0 → task watchdog + UI freeze) and
 * could spin forever when the target was beyond the bytes streamed in so far.
 * Seeking past the loaded region is safe: the decode loop's load gate just waits
 * (with vTaskDelay) until the loader streams up to the new position.
 * Caller holds s_file_mutex.
 */
static void seek_estimate(audio_engine_state_t *eng, uint32_t position_ms)
{
    uint32_t target_byte = (eng->duration_ms > 0 && eng->file_size > 0)
        ? (uint32_t)(((uint64_t)position_ms * (uint64_t)eng->file_size) / eng->duration_ms)
        : 0u;
    if (target_byte > eng->file_size) target_byte = eng->file_size;

    if (eng->file_buf) {
        eng->file_pos = target_byte;
    } else {
        fseek(eng->fp, (long)target_byte, SEEK_SET);
    }
    mp3dec_init(&eng->dec);
    eng->eof = false;
    ESP_LOGI(TAG, "Estimate seek %u ms → byte %u/%u (no PVBR/index)",
             (unsigned)position_ms, (unsigned)target_byte, (unsigned)eng->file_size);
}



/* ── Firmware decode + I2S output tasks (ESP32-P4) ────────────────────────── */
#if AE_FW
/* The producer is split into a loader + a decoder (P5b progressive preload):
 * the loader streams the file from USB into PSRAM while the decoder plays from
 * the already-loaded region. Only the loader ever touches USB, so there is never
 * a concurrent USB transfer (the condition that crashed usb_dwc_hal). This also
 * cuts load-to-play latency: playback starts after the first chunk (~0.25 s)
 * instead of waiting for the whole file (USB read is only ~1 MB/s).
 * `s_decode_pcm` is static (9 KB) to keep it off the task stack. */
static int16_t s_decode_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

#define AE_FIRST_CHUNK_BYTES (96u * 1024u)  /* min loaded before the decoder starts */
#define AE_LOAD_GATE_MARGIN  (32u * 1024u)  /* keep the decoder this far behind the loader */

static esp_err_t audio_output_service_open_codec(uint32_t sample_rate);
static esp_err_t audio_output_service_ensure_started(void);
static esp_err_t audio_output_service_stop(void);

/* Loader: read the MP3 from USB into PSRAM in chunks, publishing the watermark;
 * build the frame seek table once the whole file is in. Parks
 * until stop() so the teardown counting semaphore stays balanced. */
static void ae_loader_task(void *arg)
{
    audio_fw_task_context_t *ctx = (audio_fw_task_context_t *)arg;
    if (!audio_fw_task_context_is_bound(ctx)) {
        xSemaphoreGive(s_tasks_done);
        vTaskDelete(NULL);
        return;
    }
    audio_fw_preload_t *fw = ctx->preload;
    audio_fw_runtime_t *runtime = ctx->runtime;
    audio_engine_state_t *eng = (audio_engine_state_t *)ctx->engine;
    audio_fw_preload_begin_load(fw);

    media_io_gate_begin();
    FILE *src = fopen(fw->path, "rb");
    if (!src) {
        media_io_gate_end();
        ESP_LOGE(TAG, "Cannot open %s", fw->path);
        eng->last_error = ESP_ERR_NOT_FOUND;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "NOT FOUND");
        goto park;
    }
    fseek(src, 0, SEEK_END);
    long fsz = ftell(src);
    fseek(src, 0, SEEK_SET);
    if (fsz <= 0) {
        ESP_LOGE(TAG, "bad size %ld: %s", fsz, fw->path);
        fclose(src);
        media_io_gate_end();
        eng->last_error = ESP_ERR_INVALID_SIZE;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "BAD SIZE");
        goto park;
    }

    fw->buf = heap_caps_malloc((size_t)fsz, MALLOC_CAP_SPIRAM);
    if (!fw->buf) {
        ESP_LOGE(TAG, "PSRAM alloc %ld B failed", fsz);
        fclose(src);
        media_io_gate_end();
        eng->last_error = ESP_ERR_NO_MEM;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "NO MEM");
        goto park;
    }

    AE_LOCK();
    eng->file_buf  = fw->buf;
    eng->file_size = (size_t)fsz;
    eng->file_pos  = 0;
    eng->fp        = NULL;
    AE_UNLOCK();

    int64_t t0  = esp_timer_get_time();
    size_t  off = 0;
    while (off < (size_t)fsz && runtime->run) {
        size_t want = (size_t)fsz - off;
        if (want > (256u * 1024u)) want = 256u * 1024u;
        size_t got = fread(fw->buf + off, 1, want, src);
        if (got == 0) break;
        off += got;
        fw->loaded_bytes = off;                               /* publish watermark */
        eng->load_progress = (uint8_t)(off * 100u / (size_t)fsz);
    }
    fclose(src);
    media_io_gate_end();

    if (runtime->run && off == (size_t)fsz) {
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        ESP_LOGI(TAG, "preloaded %u KB in %lld ms (%.1f MB/s)", (unsigned)(off / 1024u),
                 (long long)dt_ms, dt_ms > 0 ? (off / 1048576.0) / (dt_ms / 1000.0) : 0.0);
        AE_LOCK();
        build_seek_table(eng);        /* full file in PSRAM → frame-accurate IFI seeks */
        AE_UNLOCK();
        fw->load_done = true;
        eng->load_progress = 100;
    }

park:
    while (runtime->run) vTaskDelay(pdMS_TO_TICKS(20));   /* stay alive until stop() */
    runtime->loader_task = NULL;
    xSemaphoreGive(s_tasks_done);
    vTaskDelete(NULL);
}

/* Decoder: plays from the loaded PSRAM region; never touches USB. */
static void ae_decode_task(void *arg)
{
    audio_fw_task_context_t *ctx = (audio_fw_task_context_t *)arg;
    if (!audio_fw_task_context_is_bound(ctx)) {
        xSemaphoreGive(s_tasks_done);
        vTaskDeleteWithCaps(NULL);
        return;
    }
    audio_fw_preload_t *fw = ctx->preload;
    audio_fw_runtime_t *runtime = ctx->runtime;
    audio_engine_state_t *eng = (audio_engine_state_t *)ctx->engine;
    audio_pcm_ring_t *pcm_ring = (audio_pcm_ring_t *)ctx->pcm_ring;
    audio_resampler_state_t *resampler = (audio_resampler_state_t *)ctx->resampler;

    /* Wait for the loader to allocate the buffer and fetch the first chunk. */
    while (runtime->run && fw->loaded_bytes < AE_FIRST_CHUNK_BYTES && !fw->load_done) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!runtime->run) goto cleanup;

    /* Latch the sample rate from the first decodable frame, then open the codec.
     * Gated: a large ID3 tag may push frame 1 past the first chunk — wait for it. */
    int attempts = 0;
    while (runtime->run && eng->sample_rate == 0 && attempts < 256 && !eng->eof) {
        if (!fw->load_done && eng->file_pos + AE_LOAD_GATE_MARGIN > fw->loaded_bytes) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        AE_LOCK();
        int n = decode_one_frame(eng, fw, s_decode_pcm);
        if (n > 0) {
            eng->frames_since_seek += (uint64_t)n;
            for (int i = 0; i < n; i++) {
                audio_pcm_ring_push(pcm_ring, s_decode_pcm[i * 2], s_decode_pcm[i * 2 + 1]);
            }
        }
        AE_UNLOCK();
        attempts++;
    }
    if (!runtime->run || eng->sample_rate == 0) {
        ESP_LOGE(TAG, "no audio frame found");
        eng->last_error = ESP_FAIL;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "NO AUDIO FRAME");
        goto cleanup;
    }

    if (audio_output_service_open_codec(eng->sample_rate) != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open(%u Hz) failed", (unsigned)eng->sample_rate);
        eng->last_error = ESP_FAIL;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "CODEC OPEN ERR");
        goto cleanup;
    }
    ESP_LOGI(TAG, "producer ready @ %u Hz, shared output mixer eligible", (unsigned)eng->sample_rate);
    eng->load_progress = 100;
    eng->loading       = false;   /* P5a: track is now playable */

    /* Steady-state decode loop (reads from PSRAM memory — no USB). */
    while (runtime->run) {
        if (eng->seek_requested) {
            AE_LOCK();
            if (eng->seek_requested) {
                uint32_t target_ms = eng->seek_target_ms;
                if (eng->seek_table) {
                    seek_index(eng, target_ms);
                } else if (eng->has_pvbr) {
                    seek_pvbr(eng, target_ms);
                } else {
                    seek_estimate(eng, target_ms);
                }
                eng->seek_base_ms      = target_ms;
                eng->frames_since_seek = 0u;
                if (!eng->seek_is_loop) {
                    eng->output_base_ms = target_ms;
                    eng->output_frames_since_seek = 0u;
                }
                eng->eof               = false;
                mp3dec_init(&eng->dec);

                /* Loop wrap keeps the ring (gapless); user seeks flush it. */
                if (!eng->seek_is_loop) {
                    audio_pcm_ring_reset(pcm_ring);
                    audio_resampler_reset(resampler);
                }
                eng->seek_is_loop   = false;
                eng->seek_requested = false;
            }
            AE_UNLOCK();
        }

        /* Gate: never decode past what the loader has fetched into PSRAM. */
        if (!fw->load_done && eng->file_pos + AE_LOAD_GATE_MARGIN > fw->loaded_bytes) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (eng->eof || audio_pcm_ring_free(pcm_ring) < (uint32_t)MINIMP3_MAX_SAMPLES_PER_FRAME) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        AE_LOCK();
        int  samples = decode_one_frame(eng, fw, s_decode_pcm);
        if (samples > 0) {
            eng->frames_since_seek += (uint64_t)samples;
            if (eng->loop_active && eng->sample_rate > 0) {
                uint32_t current_ms = eng->seek_base_ms + (uint32_t)(eng->frames_since_seek * 1000u / eng->sample_rate);
                if (current_ms >= eng->loop_end_ms) {
                    eng->seek_target_ms = eng->loop_start_ms;
                    eng->seek_is_loop   = true;   /* gapless: keep the ring */
                    eng->seek_requested = true;
                }
            }
        }
        bool eof = eng->eof;
        AE_UNLOCK();

        if (eof && samples <= 0) {
            eng->playing = false;
            while (eng->eof && runtime->run) vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (samples <= 0) continue;

        for (int i = 0; i < samples && runtime->run; i++) {
            while (audio_pcm_ring_free(pcm_ring) == 0 && runtime->run) vTaskDelay(pdMS_TO_TICKS(1));
            audio_pcm_ring_push(pcm_ring, s_decode_pcm[i * 2], s_decode_pcm[i * 2 + 1]);
        }
    }

cleanup:
    /* The preload buffer / file are owned by the loader + audio_engine_stop(). */
    runtime->decode_task = NULL;
    xSemaphoreGive(s_tasks_done);
    vTaskDeleteWithCaps(NULL);
}

/* Consumer: pitch-resample from the ring and write PCM to the ES8311.
 * esp_codec_dev_write() blocks on the I2S DMA, which paces real-time playback. */
#define AE_OUT_FRAMES 256
static esp_err_t audio_output_service_open_codec(uint32_t sample_rate)
{
    if (sample_rate == 0) return ESP_ERR_INVALID_ARG;

    AE_LOCK();
    if (s_output_codec_open) {
        AE_UNLOCK();
        return ESP_OK;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .sample_rate     = sample_rate,
    };
    if (esp_codec_dev_open(s_codec, &fs) != 0) {
        AE_UNLOCK();
        return ESP_FAIL;
    }
    s_output_codec_open = true;
    s_output_sample_rate = sample_rate;
    ESP_LOGI(TAG, "shared codec open @ %u Hz", (unsigned)sample_rate);
    AE_UNLOCK();
    return ESP_OK;
}

static void ae_output_task(void *arg)
{
    (void)arg;
    int16_t out[AE_OUT_FRAMES * 2];
    while (s_output_run) {
        if (!s_output_codec_open) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        float deck0_gain = 1.0f;
        float deck1_gain = 1.0f;
        audio_engine_get_output_gains(&deck0_gain, &deck1_gain);

        const uint8_t deck0_index = AUDIO_ENGINE_COMPAT_DECK;
        const uint8_t deck1_index = 1u;
        audio_output_mixer_deck_t deck0 = {
            .active = deck_output_active(deck0_index),
            .pitch_factor = s_engines[deck0_index].pitch_factor,
            .gain = deck0_gain,
            .resampler = resampler_for_deck(deck0_index),
            .pop_source = pop_ring_source,
            .source_ctx = pcm_ring_for_deck(deck0_index),
        };
        audio_output_mixer_deck_t deck1 = {
            .active = deck_output_active(deck1_index),
            .pitch_factor = s_engines[deck1_index].pitch_factor,
            .gain = deck1_gain,
            .resampler = resampler_for_deck(deck1_index),
            .pop_source = pop_ring_source,
            .source_ctx = pcm_ring_for_deck(deck1_index),
        };

        if (!deck0.active && !deck1.active) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t consumed[AUDIO_ENGINE_DECK_COUNT] = { 0 };
        for (int i = 0; i < AE_OUT_FRAMES; i++) {
            uint32_t frame_consumed0 = 0;
            uint32_t frame_consumed1 = 0;
            audio_mixer_frame_t frame = audio_output_mixer_next(&deck0,
                                                                &deck1,
                                                                &frame_consumed0,
                                                                &frame_consumed1);
            consumed[deck0_index] += frame_consumed0;
            consumed[deck1_index] += frame_consumed1;
            out[i * 2    ] = frame.left;
            out[i * 2 + 1] = frame.right;
        }
        if (esp_codec_dev_write(s_codec, out, (int)sizeof(out)) == ESP_OK) {
            AE_LOCK();
            update_deck_output_position(deck0_index, consumed[deck0_index]);
            update_deck_output_position(deck1_index, consumed[deck1_index]);
            AE_UNLOCK();
        }
    }
    if (s_output_codec_open) {
        esp_codec_dev_close(s_codec);
        s_output_codec_open = false;
        s_output_sample_rate = 0;
    }
    s_output_task = NULL;
    if (s_output_done) xSemaphoreGive(s_output_done);
    vTaskDelete(NULL);
}

static esp_err_t audio_output_service_ensure_started(void)
{
    if (s_output_task) return ESP_OK;
    if (s_output_done) {
        while (xSemaphoreTake(s_output_done, 0) == pdTRUE) {
            /* drain stale output exit signals */
        }
    }
    s_output_run = true;
    if (xTaskCreate(ae_output_task, "ae_output", 4096, NULL, 6, &s_output_task) != pdPASS) {
        s_output_run = false;
        s_output_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t audio_output_service_stop(void)
{
    if (!s_output_task) {
        if (s_output_codec_open) {
            esp_codec_dev_close(s_codec);
            s_output_codec_open = false;
            s_output_sample_rate = 0;
        }
        s_output_run = false;
        return ESP_OK;
    }

    s_output_run = false;
    if (s_output_done &&
        xSemaphoreTake(s_output_done, pdMS_TO_TICKS(1500)) != pdTRUE) {
        ESP_LOGE(TAG, "shared output stop timed out");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
#endif /* AE_FW */

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═════════════════════════════════════════════════════════════════════════ */

/* ── audio_engine_init ────────────────────────────────────────────────────── */
esp_err_t audio_engine_init(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        memset(&s_engines[i], 0, sizeof s_engines[i]);
        s_engines[i].pitch_factor = 1.0f;
        s_engines[i].load_progress = 100;
        s_engines[i].last_error = ESP_OK;
        snprintf(s_engines[i].last_error_text, sizeof(s_engines[i].last_error_text), "OK");
        mp3dec_init(&s_engines[i].dec);
    }
    s_active_eng = &s_engines[AUDIO_ENGINE_COMPAT_DECK];
    reset_all_pcm_rings();
#if AE_FW
    reset_all_resamplers();
    reset_all_fw_preloads();
    reset_all_fw_runtimes();
    reset_all_fw_task_contexts();
#endif
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        s_channel_volume[i] = AUDIO_MIXER_CONTROL_MAX;
        s_pfl_enabled[i] = false;
    }
    s_crossfader = AUDIO_MIXER_CONTROL_CENTER;

#if AE_FW
    /* Firmware: the ES8311 codec was created by bsp_audio_init(); grab the handle.
     * The I2S clock is configured per-track in audio_engine_load via codec_open. */
    s_codec = bsp_audio_get_codec_dev();
    if (!s_codec) {
        ESP_LOGE(TAG, "audio_engine_init: codec not ready (call bsp_audio_init first)");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_file_mutex) s_file_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_tasks_done) {
        s_tasks_done = xSemaphoreCreateCounting(AUDIO_ENGINE_DECK_COUNT * 3, 0);
    }
    if (!s_output_done) {
        s_output_done = xSemaphoreCreateCounting(1, 0);
    }
    if (!s_file_mutex || !s_tasks_done || !s_output_done) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "audio_engine_init: ES8311 codec ready");
#endif

    return ESP_OK;
}

/* ── audio_engine_load ────────────────────────────────────────────────────── */
esp_err_t audio_engine_load(const char     *mp3_path,
                             const uint32_t *pvbr_400,
                             uint32_t        duration_ms)
{
    s_eng.last_error = ESP_OK;
    snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "OK");

    if (!mp3_path) {
        s_eng.last_error = ESP_ERR_INVALID_ARG;
        snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "INVALID ARG");
        return ESP_ERR_INVALID_ARG;
    }

    /* Stop any running playback before loading a new track. If teardown times
     * out, keep ownership unchanged and refuse to start a second engine. */
    if (s_eng.loaded) {
        esp_err_t stop_rc = audio_engine_stop();
        if (stop_rc != ESP_OK) {
            s_eng.last_error = stop_rc;
            snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "STOP ERR");
            return stop_rc;
        }
    }

    s_eng.loading = true;   /* cleared when the codec opens (FW) / at end (PC) */
    s_eng.load_progress = 0;

#if AE_FW
    /* Firmware: the loader task preloads the file from USB into PSRAM and the
     * decoder reads that memory buffer, so the slow USB read stays off the caller
     * and off the playback/teardown path. */
    audio_fw_preload_t *fw = active_fw_preload();
    audio_fw_preload_set_path(fw, mp3_path);
    s_eng.fp = NULL;
#else
    FILE *fp = fopen(mp3_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot open: %s", mp3_path);
        s_eng.last_error = ESP_ERR_NOT_FOUND;
        snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "NOT FOUND");
        s_eng.loading = false;
        s_eng.load_progress = 100;
        return ESP_ERR_NOT_FOUND;
    }
    s_eng.fp = fp;
#endif

    s_eng.duration_ms = duration_ms;
    s_eng.sample_rate = 0u;   /* latched on first decoded frame */
    s_eng.channels    = 2;

    if (pvbr_400) {
        /* Check if the PVBR table is just filled with zeros.
         * Index 0 is always 0, so we check indexes from 1 to AUDIO_PVBR_LEN-1. */
        bool any_nonzero = false;
        for (int i = 1; i < AUDIO_PVBR_LEN; i++) {
            if (pvbr_400[i] > 0) {
                any_nonzero = true;
                break;
            }
        }
        if (any_nonzero) {
            memcpy(s_eng.pvbr, pvbr_400, AUDIO_PVBR_LEN * sizeof(uint32_t));
            s_eng.has_pvbr = true;
            ESP_LOGI(TAG, "PVBR seek table loaded and verified (has non-zero values)");
        } else {
            ESP_LOGW(TAG, "PVBR table contains only zeros! Disabling PVBR seek, using linear fallback.");
            memset(s_eng.pvbr, 0, sizeof s_eng.pvbr);
            s_eng.has_pvbr = false;
        }
    } else {
        memset(s_eng.pvbr, 0, sizeof s_eng.pvbr);
        s_eng.has_pvbr = false;
    }

    s_eng.seek_base_ms      = 0u;
    s_eng.frames_since_seek = 0u;
    s_eng.output_base_ms    = 0u;
    s_eng.output_frames_since_seek = 0u;
    s_eng.playing           = false;
    s_eng.paused            = false;
    s_eng.eof               = false;
    s_eng.loaded            = true;

    mp3dec_init(&s_eng.dec);
    ring_reset();



    ESP_LOGI(TAG, "Loaded: %s  dur=%u ms  pvbr=%s",
             mp3_path, (unsigned)duration_ms, pvbr_400 ? "yes" : "no");



#if AE_FW
    /* Keep load() light: MP3 decode uses ~13 KB of stack (4 KB read buffer + a
     * 9 KB PCM frame) and would overflow the caller (e.g. the LVGL task). The
     * decode task latches the sample rate, opens the codec, then streams. */
    audio_fw_runtime_t *runtime = active_fw_runtime();
    audio_fw_task_context_t *task_ctx = active_fw_task_context();
    audio_resampler_reset(active_resampler());
    audio_fw_runtime_begin_load(runtime);
    audio_fw_preload_begin_load(fw);
    uint8_t deck = active_deck_index();
    audio_fw_task_plan_t task_plan =
        audio_fw_task_plan_for_deck(deck,
                                    AUDIO_ENGINE_COMPAT_DECK,
                                    audio_fw_output_task_running());
    audio_fw_task_context_bind(task_ctx,
                               deck,
                               fw,
                               runtime,
                               &s_engines[deck],
                               &s_pcm_rings[deck],
                               &s_resamplers[deck],
                               task_plan);
    if (s_tasks_done) {
        while (xSemaphoreTake(s_tasks_done, 0) == pdTRUE) {
            /* drain stale task-exit signals from a previous load */
        }
    }
    /* loader: streams USB→PSRAM (sole USB user). decode: minimp3 needs a large
     * on-stack scratch (~15 KB) + linear-seek scanning → 48 KB stack. output:
     * real-time I2S writes (highest prio). */
    if (task_plan.start_loader) {
        if (xTaskCreate(ae_loader_task, "ae_loader", 4096, task_ctx, 5,
                        (TaskHandle_t *)&runtime->loader_task) == pdPASS) {
            audio_fw_runtime_mark_task_started(runtime);
        } else {
            ESP_LOGE(TAG, "failed to create ae_loader task");
        }
    }
    if (task_plan.start_decode) {
        if (xTaskCreateWithCaps(ae_decode_task, "ae_decode", 49152, task_ctx, 5,
                                (TaskHandle_t *)&runtime->decode_task,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
            audio_fw_runtime_mark_task_started(runtime);
        } else {
            ESP_LOGE(TAG, "failed to create ae_decode task");
        }
    }
    if (task_plan.start_output) {
        if (xTaskCreate(ae_output_task, "ae_output", 4096, task_ctx, 6,
                        (TaskHandle_t *)&runtime->output_task) == pdPASS) {
            audio_fw_runtime_mark_task_started(runtime);
        } else {
            ESP_LOGE(TAG, "failed to create ae_output task");
        }
    }
    esp_err_t output_rc = audio_output_service_ensure_started();
    if (output_rc != ESP_OK) {
        ESP_LOGE(TAG, "failed to start shared output task");
        s_eng.last_error = output_rc;
        snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "OUTPUT TASK ERR");
        s_eng.loading = false;
        s_eng.load_progress = 100;
        runtime->run = false;
        for (int i = 0; i < runtime->tasks_started; i++) {
            xSemaphoreTake(s_tasks_done, pdMS_TO_TICKS(1500));
        }
        if (fw->buf) {
            heap_caps_free(fw->buf);
            fw->buf = NULL;
        }
        memset(&s_eng, 0, sizeof s_eng);
        s_eng.pitch_factor = 1.0f;
        s_eng.load_progress = 100;
        s_eng.last_error = output_rc;
        snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "OUTPUT TASK ERR");
        audio_fw_runtime_mark_stopped(runtime);
        audio_fw_task_context_reset(task_ctx);
        return output_rc;
    }
    if (runtime->tasks_started != task_plan.expected_tasks) {
        s_eng.last_error = ESP_ERR_NO_MEM;
        snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "TASK CREATE ERR");
        s_eng.loading = false;
        s_eng.load_progress = 100;
        runtime->run = false;
        for (int i = 0; i < runtime->tasks_started; i++) {
            xSemaphoreTake(s_tasks_done, pdMS_TO_TICKS(1500));
        }
        if (runtime->codec_open) {
            esp_codec_dev_close(s_codec);
            runtime->codec_open = false;
        }
        if (fw->buf) {
            heap_caps_free(fw->buf);
            fw->buf = NULL;
        }
        memset(&s_eng, 0, sizeof s_eng);
        s_eng.pitch_factor = 1.0f;
        s_eng.load_progress = 100;
        s_eng.last_error = ESP_ERR_NO_MEM;
        snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "TASK CREATE ERR");
        audio_fw_runtime_mark_stopped(runtime);
        audio_fw_task_context_reset(task_ctx);
        return ESP_ERR_NO_MEM;
    }
#endif

#if !AE_FW
    /* PC paths decode on demand (no PSRAM preload) — ready immediately. */
    s_eng.loading = false;
    s_eng.load_progress = 100;
#endif

    return ESP_OK;
}

/* ── audio_engine_play ────────────────────────────────────────────────────── */
esp_err_t audio_engine_play(void)
{
    if (!s_eng.loaded) return ESP_ERR_INVALID_STATE;

    s_eng.paused  = false;
    s_eng.playing = true;
    s_eng.eof     = false; /* allow replay if previously at end */


    return ESP_OK;
}

/* ── audio_engine_pause ───────────────────────────────────────────────────── */
esp_err_t audio_engine_pause(void)
{
    if (!s_eng.loaded) return ESP_ERR_INVALID_STATE;

    s_eng.playing = false;
    s_eng.paused  = true;


    return ESP_OK;
}

/* ── audio_engine_stop ────────────────────────────────────────────────────── */
esp_err_t audio_engine_stop(void)
{
    if (!s_eng.loaded) return ESP_OK;

    s_eng.playing = false;
    s_eng.paused  = false;
    s_eng.loading = false;
    s_eng.load_progress = 100;



#if AE_FW
    audio_fw_runtime_t *runtime = active_fw_runtime();
    if (runtime->run || runtime->tasks_started > 0) {
        runtime->run = false;
        s_eng.eof = false;   /* wake decode task if parked at EOF */
        /* Wait for all started tasks before freeing buffers they can touch. */
        if (s_tasks_done) {
            int exited = 0;
            for (int i = 0; i < runtime->tasks_started; i++) {
                if (xSemaphoreTake(s_tasks_done, pdMS_TO_TICKS(1500)) == pdTRUE) {
                    exited++;
                }
            }
            if (exited != runtime->tasks_started) {
                ESP_LOGE(TAG, "audio stop timed out waiting for tasks (%d/%d exited)",
                         exited, runtime->tasks_started);
                return ESP_ERR_TIMEOUT;
            }
        }
        runtime->loader_task = NULL;
        runtime->decode_task = NULL;
        runtime->output_task = NULL;
        runtime->tasks_started = 0;
        audio_fw_task_context_reset(active_fw_task_context());
    }
#endif

    AE_LOCK();
    if (s_eng.fp) { fclose(s_eng.fp); s_eng.fp = NULL; }
    s_eng.file_buf  = NULL;
    s_eng.file_size = 0;
    s_eng.file_pos  = 0;
    if (s_eng.seek_table) {
#if AE_FW
        heap_caps_free(s_eng.seek_table);
#else
        free(s_eng.seek_table);
#endif
        s_eng.seek_table = NULL;
    }
    s_eng.seek_table_len = 0;
    AE_UNLOCK();

#if AE_FW
    /* Free the PSRAM preload buffer. */
    audio_fw_preload_t *fw = active_fw_preload();
    if (fw->buf) { heap_caps_free(fw->buf); fw->buf = NULL; }
    audio_fw_preload_begin_load(fw);
#endif

    memset(&s_eng, 0, sizeof s_eng);
    s_eng.pitch_factor = 1.0f;
    s_eng.load_progress = 100;
    s_eng.last_error = ESP_OK;
    snprintf(s_eng.last_error_text, sizeof(s_eng.last_error_text), "OK");
    ring_reset();

#if AE_FW
    if (!any_deck_loaded()) {
        esp_err_t output_rc = audio_output_service_stop();
        if (output_rc != ESP_OK) {
            return output_rc;
        }
    }
#endif

    return ESP_OK;
}

/* ── audio_engine_seek ────────────────────────────────────────────────────── */
esp_err_t audio_engine_seek(uint32_t position_ms)
{
    if (!s_eng.loaded || (!s_eng.fp && !s_eng.file_buf)) return ESP_ERR_INVALID_STATE;

    s_eng.seek_target_ms = position_ms;
    s_eng.seek_is_loop   = false;  /* user seek → flush ring (set before requested) */
    s_eng.seek_requested = true;
    s_eng.eof            = false;  /* also wakes decode thread if at EOF */

    AE_LOCK();
    s_eng.output_base_ms = position_ms;
    s_eng.output_frames_since_seek = 0u;
    s_eng.seek_base_ms = position_ms;
    s_eng.frames_since_seek = 0u;
    /* Flush stale decoded data from ring buffer to mute audio immediately */
    ring_reset();

#if AE_FW
    audio_resampler_reset(active_resampler());
#endif
    AE_UNLOCK();

    return ESP_OK;
}

/* ── audio_engine_set_pitch ───────────────────────────────────────────────── */
/*
 * raw_pitch:  0 = +10% faster, 8192 = ±0% normal, 16383 = −10% slower.
 *
 * Corrected formula (fader 0 → faster → factor > 1.0):
 *   factor = 1.0 + (8192 − raw_pitch) / 8192.0 × 0.10
 *
 * Keep audio_engine_raw_pitch_to_percent() and the UI pitch label on the same
 * sign convention so the label matches the actual resampling factor.
 */
void audio_engine_set_pitch(int16_t raw_pitch)
{
    float factor = 1.0f + ((8192.0f - (float)raw_pitch) / 8192.0f) * 0.10f;
    /* Clamp to ±20% to stay sane even if fader value is out of range */
    if (factor < 0.80f) factor = 0.80f;
    if (factor > 1.20f) factor = 1.20f;
    s_eng.pitch_factor = factor;
}

float audio_engine_raw_pitch_to_percent(int16_t raw_pitch)
{
    return ((8192.0f - (float)raw_pitch) / 8192.0f) * 10.0f;
}

/* ── audio_engine_position_ms ─────────────────────────────────────────────── */
uint32_t audio_engine_position_ms(void)
{
    AE_LOCK();
    if (!s_eng.loaded || s_eng.sample_rate == 0) {
        uint32_t base = s_eng.output_base_ms;
        AE_UNLOCK();
        return base;
    }
    uint32_t from_frames = (uint32_t)(s_eng.output_frames_since_seek * 1000u / s_eng.sample_rate);
    uint32_t pos = s_eng.output_base_ms + from_frames;
    AE_UNLOCK();
    return pos;
}

/* ── audio_engine_is_playing ──────────────────────────────────────────────── */
bool audio_engine_is_playing(void)
{
    return s_eng.playing && !s_eng.paused;
}

static bool deck_is_valid(uint8_t deck);

static ae_state_t engine_lifecycle_state(const audio_engine_state_t *eng)
{
    if (!eng) return AE_IDLE;
    if (eng->last_error != ESP_OK) return AE_ERROR;
    if (!eng->loaded) return AE_IDLE;
    if (eng->loading) return AE_LOADING;
    return (eng->playing && !eng->paused) ? AE_PLAYING : AE_READY;
}

esp_err_t audio_engine_deck_get_status(uint8_t deck, audio_engine_deck_status_t *out)
{
    if (!deck_is_valid(deck) || !out) return ESP_ERR_INVALID_ARG;

    audio_engine_state_t *eng = &s_engines[deck];
    memset(out, 0, sizeof(*out));

    AE_LOCK();
    out->state = engine_lifecycle_state(eng);
    out->load_progress = eng->load_progress;
    out->last_error = eng->last_error;
    snprintf(out->last_error_text, sizeof(out->last_error_text), "%s", eng->last_error_text);
    out->loaded = eng->loaded;
    out->playing = eng->playing && !eng->paused;
    if (!eng->loaded || eng->sample_rate == 0) {
        out->position_ms = eng->output_base_ms;
    } else {
        uint32_t from_frames = (uint32_t)(eng->output_frames_since_seek * 1000u / eng->sample_rate);
        out->position_ms = eng->output_base_ms + from_frames;
    }
    AE_UNLOCK();

    return ESP_OK;
}

/* ── audio_engine_get_state / load_progress (P5a) ─────────────────────────── */
ae_state_t audio_engine_get_state(void)
{
    return engine_lifecycle_state(&s_eng);
}

uint8_t audio_engine_load_progress(void)
{
    return s_eng.load_progress;
}

esp_err_t audio_engine_last_error(void)
{
    return s_eng.last_error;
}

const char *audio_engine_last_error_text(void)
{
    return s_eng.last_error_text;
}

/* ── audio_engine_set_loop ────────────────────────────────────────────────── */
void audio_engine_set_loop(uint32_t start_ms, uint32_t end_ms)
{
    AE_LOCK();
    s_eng.loop_start_ms = start_ms;
    s_eng.loop_end_ms   = end_ms;
    s_eng.loop_active   = true;
    AE_UNLOCK();
    ESP_LOGI(TAG, "Audio loop set: %lu ms to %lu ms", (unsigned long)start_ms, (unsigned long)end_ms);
}

/* ── audio_engine_clear_loop ──────────────────────────────────────────────── */
void audio_engine_clear_loop(void)
{
    AE_LOCK();
    s_eng.loop_active   = false;
    AE_UNLOCK();
    ESP_LOGI(TAG, "Audio loop cleared");
}

/* ── audio_engine_get_loop_state ───────────────────────────────────────────── */
void audio_engine_get_loop_state(bool *active, uint32_t *start_ms, uint32_t *end_ms)
{
    AE_LOCK();
    if (active) *active = s_eng.loop_active;
    if (start_ms) *start_ms = s_eng.loop_start_ms;
    if (end_ms) *end_ms = s_eng.loop_end_ms;
    AE_UNLOCK();
}

static audio_engine_state_t *select_engine(uint8_t deck);
static void restore_engine(audio_engine_state_t *prev);
#if AE_FW
static bool deck_transport_supported(uint8_t deck);
#endif

esp_err_t audio_engine_deck_set_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    audio_engine_set_loop(start_ms, end_ms);
    restore_engine(prev);
    return ESP_OK;
}

esp_err_t audio_engine_deck_clear_loop(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    audio_engine_clear_loop();
    restore_engine(prev);
    return ESP_OK;
}

esp_err_t audio_engine_deck_get_loop_state(uint8_t deck,
                                           bool *active,
                                           uint32_t *start_ms,
                                           uint32_t *end_ms)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    audio_engine_get_loop_state(active, start_ms, end_ms);
    restore_engine(prev);
    return ESP_OK;
}

static bool deck_is_valid(uint8_t deck)
{
    return deck < AUDIO_ENGINE_DECK_COUNT;
}

#if AE_FW
static bool deck_transport_supported(uint8_t deck)
{
    return audio_fw_task_plan_for_deck(deck,
                                       AUDIO_ENGINE_COMPAT_DECK,
                                       true).transport_supported;
}
#endif

static audio_engine_state_t *select_engine(uint8_t deck)
{
    audio_engine_state_t *prev = s_active_eng;
    s_active_eng = &s_engines[deck];
    return prev;
}

static void restore_engine(audio_engine_state_t *prev)
{
    s_active_eng = prev;
}

esp_err_t audio_engine_deck_load(uint8_t deck,
                                 const char *mp3_path,
                                 const uint32_t *pvbr_400,
                                 uint32_t duration_ms)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    audio_engine_state_t *prev = select_engine(deck);
    esp_err_t rc = audio_engine_load(mp3_path, pvbr_400, duration_ms);
    restore_engine(prev);
    return rc;
}

esp_err_t audio_engine_deck_play(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    esp_err_t rc = audio_engine_play();
    restore_engine(prev);
    return rc;
}

esp_err_t audio_engine_deck_pause(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    esp_err_t rc = audio_engine_pause();
    restore_engine(prev);
    return rc;
}

esp_err_t audio_engine_deck_stop(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    audio_engine_state_t *prev = select_engine(deck);
    esp_err_t rc = audio_engine_stop();
    restore_engine(prev);
    return rc;
}

esp_err_t audio_engine_stop_all(void)
{
    esp_err_t first_err = ESP_OK;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        esp_err_t rc = audio_engine_deck_stop(deck);
        if (first_err == ESP_OK && rc != ESP_OK) {
            first_err = rc;
        }
    }
#if AE_FW
    esp_err_t output_rc = audio_output_service_stop();
    if (first_err == ESP_OK && output_rc != ESP_OK) {
        first_err = output_rc;
    }
#endif
    return first_err;
}

esp_err_t audio_engine_deck_seek(uint8_t deck, uint32_t position_ms)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    esp_err_t rc = audio_engine_seek(position_ms);
    restore_engine(prev);
    return rc;
}

void audio_engine_deck_set_pitch(uint8_t deck, int16_t raw_pitch)
{
    if (!deck_is_valid(deck)) return;
#if AE_FW
    if (!deck_transport_supported(deck)) return;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    audio_engine_set_pitch(raw_pitch);
    restore_engine(prev);
}

uint32_t audio_engine_deck_position_ms(uint8_t deck)
{
    if (!deck_is_valid(deck)) return 0;
#if AE_FW
    if (!deck_transport_supported(deck)) return 0;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    uint32_t pos = audio_engine_position_ms();
    restore_engine(prev);
    return pos;
}

bool audio_engine_deck_is_playing(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
#if AE_FW
    if (!deck_transport_supported(deck)) return false;
#endif
    audio_engine_state_t *prev = select_engine(deck);
    bool playing = audio_engine_is_playing();
    restore_engine(prev);
    return playing;
}

esp_err_t audio_engine_set_channel_volume(uint8_t deck, uint16_t raw_volume)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    if (raw_volume > AUDIO_MIXER_CONTROL_MAX) {
        raw_volume = AUDIO_MIXER_CONTROL_MAX;
    }
    s_channel_volume[deck] = raw_volume;
    return ESP_OK;
}

esp_err_t audio_engine_set_crossfader(uint16_t raw_crossfader)
{
    if (raw_crossfader > AUDIO_MIXER_CONTROL_MAX) {
        raw_crossfader = AUDIO_MIXER_CONTROL_MAX;
    }
    s_crossfader = raw_crossfader;
    return ESP_OK;
}

void audio_engine_get_output_gains(float *deck0_gain, float *deck1_gain)
{
    float xf0 = 1.0f;
    float xf1 = 1.0f;
    audio_mixer_crossfader_gains(s_crossfader, &xf0, &xf1);

    if (deck0_gain) {
        *deck0_gain = audio_mixer_fader_gain(s_channel_volume[0]) * xf0;
    }
    if (deck1_gain) {
        *deck1_gain = audio_mixer_fader_gain(s_channel_volume[1]) * xf1;
    }
}

esp_err_t audio_engine_toggle_pfl(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    s_pfl_enabled[deck] = !s_pfl_enabled[deck];
    return ESP_OK;
}

bool audio_engine_get_pfl_enabled(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
    return s_pfl_enabled[deck];
}

void audio_engine_get_mixer_snapshot(audio_engine_mixer_snapshot_t *out_snapshot)
{
    if (!out_snapshot) return;
    float gain0 = 0.0f;
    float gain1 = 0.0f;
    audio_engine_get_output_gains(&gain0, &gain1);
    out_snapshot->channel_volume[0] = s_channel_volume[0];
    out_snapshot->channel_volume[1] = s_channel_volume[1];
    out_snapshot->crossfader = s_crossfader;
    out_snapshot->output_gain[0] = gain0;
    out_snapshot->output_gain[1] = gain1;
    out_snapshot->pfl_enabled[0] = s_pfl_enabled[0];
    out_snapshot->pfl_enabled[1] = s_pfl_enabled[1];
}


/* ═══════════════════════════════════════════════════════════════════════════
 * PC test helper — decode to WAV file (AUDIO_ENGINE_PC_TEST only)
 * ═════════════════════════════════════════════════════════════════════════ */
#if defined(AUDIO_ENGINE_PC_TEST)

/*
 * Write a 44-byte PCM WAV header.
 * Call once with pcm_bytes=0 as a placeholder, then rewind and call again
 * with the real byte count once encoding is complete.
 */
static void wav_write_header(FILE      *wav,
                              uint32_t   sample_rate,
                              uint16_t   channels,
                              uint32_t   pcm_bytes)
{
    const uint16_t bits        = 16u;
    const uint16_t fmt_pcm     = 1u;
    const uint32_t byte_rate   = sample_rate * channels * (bits / 8u);
    const uint16_t block_align = (uint16_t)(channels * (bits / 8u));
    const uint32_t chunk_size  = 36u + pcm_bytes;
    const uint32_t fmt_size    = 16u;

    /* RIFF chunk */
    fwrite("RIFF",       1, 4, wav);
    fwrite(&chunk_size,  4, 1, wav);
    fwrite("WAVE",       1, 4, wav);
    /* fmt  sub-chunk */
    fwrite("fmt ",       1, 4, wav);
    fwrite(&fmt_size,    4, 1, wav);
    fwrite(&fmt_pcm,     2, 1, wav);
    fwrite(&channels,    2, 1, wav);
    fwrite(&sample_rate, 4, 1, wav);
    fwrite(&byte_rate,   4, 1, wav);
    fwrite(&block_align, 2, 1, wav);
    fwrite(&bits,        2, 1, wav);
    /* data sub-chunk */
    fwrite("data",       1, 4, wav);
    fwrite(&pcm_bytes,   4, 1, wav);
}

/*
 * audio_engine_decode_to_wav — decode the loaded track to a WAV file.
 *
 * @param wav_path        Output path (will be created/truncated).
 * @param max_duration_ms Stop after this many ms of audio (0 = entire track).
 */
esp_err_t audio_engine_decode_to_wav(const char *wav_path, uint32_t max_duration_ms)
{
    if (!s_eng.loaded || !s_eng.fp) return ESP_ERR_INVALID_STATE;
    if (!wav_path)                   return ESP_ERR_INVALID_ARG;

    FILE *wav = fopen(wav_path, "wb");
    if (!wav) {
        ESP_LOGE(TAG, "Cannot create WAV: %s", wav_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Write placeholder header; patched with real sizes at end */
    wav_write_header(wav, 44100u, 2u, 0u);

    /* Rewind input, reset decoder */
    rewind(s_eng.fp);
    mp3dec_init(&s_eng.dec);
    s_eng.frames_since_seek = 0u;
    s_eng.eof               = false;

    int16_t  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    uint32_t pcm_bytes   = 0u;
    uint32_t sample_rate = 0u;
    uint16_t channels    = 2u;

    while (true) {
        AE_LOCK();
        int samples = decode_one_frame(&s_eng, pcm);
        if (samples > 0) s_eng.frames_since_seek += (uint64_t)samples;
        bool eof = s_eng.eof;
        AE_UNLOCK();

        if (eof || samples <= 0) break;

        /* Latch format on first real audio frame */
        if (sample_rate == 0u && s_eng.sample_rate > 0u) {
            sample_rate = s_eng.sample_rate;
            channels    = (s_eng.channels == 1) ? 2u : (uint16_t)s_eng.channels;
        }

        /* Respect optional duration limit */
        if (max_duration_ms > 0u && sample_rate > 0u) {
            uint32_t pos = (uint32_t)(s_eng.frames_since_seek * 1000u / sample_rate);
            if (pos >= max_duration_ms) break;
        }

        size_t written = fwrite(pcm, sizeof(int16_t), (size_t)(samples * 2), wav);
        pcm_bytes += (uint32_t)(written * sizeof(int16_t));
    }

    /* Patch WAV header with real sizes */
    rewind(wav);
    if (sample_rate == 0u) sample_rate = 44100u;
    wav_write_header(wav, sample_rate, channels, pcm_bytes);
    fclose(wav);

    double dur_s = (sample_rate > 0u && channels > 0u)
                   ? (double)pcm_bytes / (double)(sample_rate * channels * 2u)
                   : 0.0;
    ESP_LOGI(TAG, "WAV: %s  %.1f s  %u bytes  %u Hz %u ch",
             wav_path, dur_s, (unsigned)pcm_bytes,
             (unsigned)sample_rate, (unsigned)channels);
    return ESP_OK;
}

#endif /* AUDIO_ENGINE_PC_TEST */
