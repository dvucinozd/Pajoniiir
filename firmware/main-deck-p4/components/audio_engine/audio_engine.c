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
#if defined(AUDIO_ENGINE_PC_TEST)
#ifndef AUDIO_DECODER_PC_TEST
#define AUDIO_DECODER_PC_TEST
#endif
#ifndef MEDIA_IO_GATE_STANDALONE_TEST
#define MEDIA_IO_GATE_STANDALONE_TEST
#endif
#endif
#include "audio_decoder.h"
#include "audio_format.h"
#include "audio_diag.h"
#include "audio_delay_fx.h"
#include "audio_filter.h"
#include "audio_flanger_fx.h"
#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"
#include "audio_fw_task_context.h"
#include "audio_fw_task_plan.h"
#include "audio_mixer.h"
#include "audio_output_mixer.h"
#include "audio_output_timing.h"
#include "audio_pad_fx.h"
#include "audio_pcm_ring.h"
#include "audio_pcm_timeline.h"
#include "audio_scratch_buffer.h"
#include "audio_scratch.h"
#include "audio_resampler.h"
#include "audio_smart_cfx.h"
#include "monitor_pcm_link.h"

#include <math.h>
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

/* Firmware (ESP32-P4): real-time I2S output through the PCM5102A MAIN out */
#if !AE_PC
#   define AE_FW 1
#   include "freertos/FreeRTOS.h"
#   include "freertos/task.h"
#   include "freertos/semphr.h"
#   include "freertos/idf_additions.h"
#   include "esp_heap_caps.h"
#   include "esp_system.h"
#   include "esp_timer.h"
#   include "bsp_jc4880.h"
#   include "esp_codec_dev.h"
#   include "driver/i2s_common.h"
/* Declarations only — DR_FLAC_IMPLEMENTATION lives in audio_flac_decoder.c. */
#   include "dr_flac.h"
#else
#   define AE_FW 0
#endif

#if AE_FW
static int64_t ae_now_us(void)
{
    return esp_timer_get_time();
}
#endif

/* ── PCM ring buffers (stereo int16 PCM frames) ───────────────────────────── *
 *
 * Producer: decode thread (PC) / decode task (firmware).
 * Consumer: SDL audio callback (PC simulator) or codec/I2S output task (firmware).
 */
static audio_pcm_ring_t   s_pcm_rings[AUDIO_ENGINE_DECK_COUNT];

/* Canonical per-deck PCM store (batch 3B). At 48 kHz the four-second store is
 * 768 KiB/deck in PSRAM. Decode holds roughly two seconds ahead of play_seq;
 * the remaining capacity becomes bidirectional scratch history. If allocation
 * fails, that deck transparently keeps the legacy ring + scratch capture path. */
#define AE_TIMELINE_SECONDS        4u
#define AE_TIMELINE_FORWARD_MS     2000u
#define AE_TIMELINE_MAX_RATE       48000u
#define AE_TIMELINE_CAPACITY_FRAMES (AE_TIMELINE_SECONDS * AE_TIMELINE_MAX_RATE)
static audio_pcm_timeline_t s_pcm_timelines[AUDIO_ENGINE_DECK_COUNT];
static int16_t             *s_pcm_timeline_storage[AUDIO_ENGINE_DECK_COUNT];
/* Sole writer is the output task; diagnostics reads are best-effort snapshots. */
static uint32_t s_pcm_underrun_count[AUDIO_ENGINE_DECK_COUNT];

/* Per-deck scratch capture buffer (vinyl mode Phase 2). The decode task appends
 * every decoded source frame here in addition to the PCM ring, giving a rolling
 * random-access window of recent audio for the (future) scratch read head. The
 * PSRAM backing store is allocated once at init; capacity is fixed for
 * AE_SCRATCH_SECONDS at AE_SCRATCH_MAX_RATE (hi-res sources get a shorter window
 * since the store holds source frames). Capture is passive — playback unchanged.
 * See docs/VINYL_SCRATCH_PLAN.md. */
#define AE_SCRATCH_SECONDS   4u
#define AE_SCRATCH_MAX_RATE  48000u
#define AE_SCRATCH_CAPACITY_FRAMES (AE_SCRATCH_SECONDS * AE_SCRATCH_MAX_RATE)
static audio_scratch_buffer_t s_scratch_buf[AUDIO_ENGINE_DECK_COUNT];
static int16_t               *s_scratch_storage[AUDIO_ENGINE_DECK_COUNT];

/* Scratch playback (vinyl mode Phase 4): while s_scratch_playing[deck] is set,
 * the output task draws that deck's frames from s_scratch_engine[deck] (a
 * jog-driven read over s_scratch_buf[deck]) instead of the resampler+ring, and
 * the decode task freezes capture so the window's newest frame stays put under
 * the read head. Plain atomic bool: set/cleared by the control task, read by the
 * output + decode tasks. s_scratch_ctx_deck feeds the deck index to the mixer's
 * scratch render callback. */
static audio_scratch_t   s_scratch_engine[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_playing[AUDIO_ENGINE_DECK_COUNT];
static uint8_t           s_scratch_ctx_deck[AUDIO_ENGINE_DECK_COUNT];
/* Output sets this after fade-out no longer needs the frozen history. Decode
 * consumes it before any post-seek capture, so two discontinuous timelines can
 * never be appended into the same scratch window. */
static bool              s_scratch_capture_reset_ready[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_capture_freeze[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_capture_writing[AUDIO_ENGINE_DECK_COUNT];
static uint32_t          s_scratch_head_back_bits[AUDIO_ENGINE_DECK_COUNT];
static uint32_t          s_scratch_handoff_consumed[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_abort_seek_requested[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_abort_seek_waiting[AUDIO_ENGINE_DECK_COUNT];
static uint32_t          s_scratch_abort_seek_target_ms[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_regrab_requested[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_started_paused[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_return_paused[AUDIO_ENGINE_DECK_COUNT];
static uint32_t          s_scratch_origin_pos_ms[AUDIO_ENGINE_DECK_COUNT];
static uint32_t          s_scratch_origin_play_seq[AUDIO_ENGINE_DECK_COUNT];

/* Click-free handoff (vinyl mode Phase 4b). On release the output does not snap
 * from the scratch source to forward playback; it cross-fades per sample:
 * FADE_OUT ramps the scratch tail to silence, FADE_IN ramps the resumed forward
 * audio (popped from the just-seeked ring) up from silence — waiting at silence
 * if the ring has not refilled yet — then RING hands back to the resampler at the
 * next block. All owned by the output task except the FADE_OUT arm set by the
 * control task in scratch_end. */
typedef enum {
    AE_SCRATCH_HANDOFF_NONE = 0,
    AE_SCRATCH_HANDOFF_FADE_OUT,
    AE_SCRATCH_HANDOFF_FADE_IN,
    AE_SCRATCH_HANDOFF_RING,
} ae_scratch_handoff_t;

typedef enum {
    AE_SEEK_REASON_USER = 0,
    AE_SEEK_REASON_LOOP,
    AE_SEEK_REASON_SCRATCH_RELEASE,
    AE_SEEK_REASON_SCRATCH_ABORT,
} ae_seek_reason_t;
/* uint8_t (not the enum type) so the phase can be accessed with the u8
 * release/acquire helpers above; values are the ae_scratch_handoff_t constants. */
static uint8_t              s_scratch_handoff[AUDIO_ENGINE_DECK_COUNT];
static float                s_scratch_handoff_gain[AUDIO_ENGINE_DECK_COUNT];
#define AE_SCRATCH_XFADE_FRAMES 480u   /* ~10 ms per side @ 48 kHz */
#define AE_SCRATCH_XFADE_STEP   (1.0f / (float)AE_SCRATCH_XFADE_FRAMES)

/* Shared scratchpad buffer for decoding to avoid stack allocation */
static int16_t            s_scratch_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2u];

static void reset_all_pcm_rings(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_pcm_ring_reset(&s_pcm_rings[i]);
    }
}

static bool deck_is_valid(uint8_t deck);
static void init_beat_fx_echo_buffers(void);
static void init_beat_fx_flanger_buffers(void);
static void init_pad_fx_buffers(void);
static void init_scratch_buffers(void);
static esp_err_t audio_engine_seek_for_deck_reason(uint8_t deck,
                                                   uint32_t position_ms,
                                                   ae_seek_reason_t reason);

static float pregain_gain_from_raw(uint16_t raw)
{
    if (raw > AUDIO_MIXER_CONTROL_MAX) {
        raw = AUDIO_MIXER_CONTROL_MAX;
    }
    if (raw <= AUDIO_MIXER_CONTROL_CENTER) {
        float t = (float)raw / (float)AUDIO_MIXER_CONTROL_CENTER;
        return 0.25f + (0.75f * t);
    }
    float t = (float)(raw - AUDIO_MIXER_CONTROL_CENTER) /
              (float)(AUDIO_MIXER_CONTROL_MAX - AUDIO_MIXER_CONTROL_CENTER);
    return 1.0f + t;
}

/* ── Engine state ─────────────────────────────────────────────────────────── */
typedef struct {
    FILE    *fp;
    mp3dec_t dec;
    audio_format_t format;
    audio_decoder_t decoder;
    bool decoder_open;

    /* Direct memory-mapped buffer for firmware (bypasses fmemopen bugs) */
    const uint8_t *file_buf;
    size_t         file_size;
    size_t         file_pos;

    /* Firmware WAV decode state for the PSRAM preloaded buffer. */
    bool           wav_ready;
    size_t         wav_data_offset;
    size_t         wav_data_size;
    size_t         wav_data_pos;
    uint16_t       wav_block_align;
    uint64_t       wav_total_frames;
    uint64_t       wav_current_frame;

    /* Firmware FLAC decode over the PSRAM preloaded buffer (dr_flac).
     * void* to keep dr_flac.h out of the struct definition; cast in ae_flac_*. */
    void          *flac;
    bool           flac_ready;

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

    /* Paused/CUE seek pre-roll: decode starts before the requested position,
     * then moves canonical play_seq to this frame once history is published. */
    uint32_t timeline_preroll_frames;
    bool     timeline_preroll_pending;

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
    volatile uint8_t  seek_reason;

    /* Real-time loop */
    volatile uint32_t loop_start_ms;
    volatile uint32_t loop_end_ms;
    volatile bool     loop_active;

    /* Instant Frame-Index Seek */
    uint32_t *seek_table;
    uint32_t  seek_table_len;
} audio_engine_state_t;

static audio_engine_state_t  s_engines[AUDIO_ENGINE_DECK_COUNT];

static inline audio_pcm_ring_t *pcm_ring_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_pcm_rings[deck];
    }
    return &s_pcm_rings[AUDIO_ENGINE_COMPAT_DECK];
}

static inline audio_scratch_buffer_t *scratch_buffer_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_scratch_buf[deck];
    }
    return &s_scratch_buf[AUDIO_ENGINE_COMPAT_DECK];
}

static uint16_t         s_channel_volume[AUDIO_ENGINE_DECK_COUNT] = {
    AUDIO_MIXER_CONTROL_MAX,
    AUDIO_MIXER_CONTROL_MAX,
};
static uint16_t         s_pregain[AUDIO_ENGINE_DECK_COUNT] = {
    AUDIO_MIXER_CONTROL_CENTER,
    AUDIO_MIXER_CONTROL_CENTER,
};
static uint16_t         s_crossfader = AUDIO_MIXER_CONTROL_CENTER;
static float            s_master_trim = 1.0f;
static uint16_t         s_master_volume = AUDIO_MIXER_CONTROL_MAX;
static uint16_t         s_headphone_mix = AUDIO_MIXER_CONTROL_MAX;
static uint16_t         s_headphone_level = AUDIO_MIXER_CONTROL_MAX;
static bool             s_master_cue_enabled = true;
static bool             s_pfl_enabled[AUDIO_ENGINE_DECK_COUNT];
static uint8_t          s_cue_mode = 0; /* 0 = stereo master, 1 = split mono */
static audio_headphone_mode_t s_headphone_mode = AUDIO_HEADPHONE_MODE_MASTER_MONO;
static uint16_t         s_deck_peak[AUDIO_ENGINE_DECK_COUNT];
static uint16_t         s_deck_ui_peak[AUDIO_ENGINE_DECK_COUNT];
static audio_mixer_limiter_stats_t s_limiter_stats;
static audio_eq_state_t s_deck_eq[AUDIO_ENGINE_DECK_COUNT];
static audio_filter_state_t s_deck_filter[AUDIO_ENGINE_DECK_COUNT];
static uint16_t         s_deck_filter_raw[AUDIO_ENGINE_DECK_COUNT];
static uint16_t         s_deck_filter_effective[AUDIO_ENGINE_DECK_COUNT];
static audio_filter_state_t s_beat_fx_filter[AUDIO_ENGINE_DECK_COUNT];
static bool             s_beat_fx_filter_enabled[AUDIO_ENGINE_DECK_COUNT];
static audio_delay_fx_t s_beat_fx_echo[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_echo_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_echo_right[AUDIO_ENGINE_DECK_COUNT];
static bool             s_beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
static audio_flanger_fx_t s_beat_fx_flanger[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_flanger_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_flanger_right[AUDIO_ENGINE_DECK_COUNT];
static bool             s_beat_fx_flanger_enabled[AUDIO_ENGINE_DECK_COUNT];
static audio_pad_fx_state_t s_pad_fx[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_pad_fx_echo_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_pad_fx_echo_right[AUDIO_ENGINE_DECK_COUNT];
static bool             s_smart_cfx_enabled;
static bool             s_smart_fader_enabled;
/* Transient jog pitch-bend (nudge) per deck: a jog while playing bumps this, the
 * output task adds it on top of pitch_factor and decays it back to 0, so tempo
 * returns to the fader setting when the jog stops. Plain float — written by the
 * control task, read+decayed by the output task; a 32-bit aligned store is not
 * torn and the effect is transient, so no lock (matches pitch_factor access).
 * Feel constants — tune on hardware: each jog tick adds *_PER_TICK (clamped to
 * ±*_MAX = a momentary tempo change); the output task multiplies toward 0 by
 * *_DECAY each ~5.8 ms block so tempo springs back to the fader on release. */
#define AE_JOG_BEND_PER_TICK 0.02f
#define AE_JOG_BEND_MAX      0.30f
#define AE_JOG_BEND_DECAY    0.88f
static float            s_jog_bend[AUDIO_ENGINE_DECK_COUNT];
static float            s_pending_pitch_factor[AUDIO_ENGINE_DECK_COUNT];
static bool             s_pending_pitch_valid[AUDIO_ENGINE_DECK_COUNT];

/* Platter-hold (vinyl mode Phase 1): while the jog platter top is touched during
 * playback, deck_core sets this so the deck output is silenced and its position
 * frozen (an output-level mute, the logical play state stays "playing" for LEDs).
 * Releasing clears it and forward playback resumes instantly from wherever the
 * position was scrubbed to. Plain atomic bool: written by the control task, read
 * by the output task; no lock (matches the s_jog_bend / pitch_factor pattern). */
static bool             s_deck_hold[AUDIO_ENGINE_DECK_COUNT];

static inline uint16_t atomic_load_u16(const uint16_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static inline void atomic_store_u16(uint16_t *value, uint16_t new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELAXED);
}

static inline uint32_t atomic_load_u32(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static inline void atomic_store_u32(uint32_t *value, uint32_t new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELAXED);
}

static inline bool atomic_load_bool(const bool *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline bool timeline_active(uint8_t deck)
{
    return deck < AUDIO_ENGINE_DECK_COUNT &&
           s_pcm_timelines[deck].frames != NULL &&
           s_pcm_timelines[deck].capacity > 0u;
}

static bool pop_deck_source(void *ctx, audio_mixer_frame_t *out_frame)
{
    uint8_t deck = ctx ? *(const uint8_t *)ctx : AUDIO_ENGINE_COMPAT_DECK;
    if (deck >= AUDIO_ENGINE_DECK_COUNT) deck = AUDIO_ENGINE_COMPAT_DECK;
    if (timeline_active(deck)) {
        bool ok = audio_pcm_timeline_pop(&s_pcm_timelines[deck], out_frame);
        if (!ok) s_pcm_underrun_count[deck]++;
        return ok;
    }
    bool ok = audio_pcm_ring_pop(&s_pcm_rings[deck], out_frame);
    if (!ok) s_pcm_underrun_count[deck]++;
    return ok;
}

static uint32_t deck_pcm_used(uint8_t deck)
{
    return timeline_active(deck)
        ? audio_pcm_timeline_future_frames(&s_pcm_timelines[deck])
        : audio_pcm_ring_used(&s_pcm_rings[deck]);
}

static uint32_t deck_pcm_free(uint8_t deck, uint32_t sample_rate)
{
    if (!timeline_active(deck)) return audio_pcm_ring_free(&s_pcm_rings[deck]);
    uint32_t target = sample_rate > 0u
        ? (uint32_t)(((uint64_t)sample_rate * AE_TIMELINE_FORWARD_MS) / 1000u)
        : AUDIO_PCM_RING_FRAMES;
    if (target > s_pcm_timelines[deck].capacity) target = s_pcm_timelines[deck].capacity;
    uint32_t future = audio_pcm_timeline_future_frames(&s_pcm_timelines[deck]);
    return future < target ? target - future : 0u;
}

static void deck_pcm_reset(uint8_t deck)
{
    audio_pcm_ring_reset(&s_pcm_rings[deck]);
    if (timeline_active(deck)) audio_pcm_timeline_reset(&s_pcm_timelines[deck]);
}

static bool deck_pcm_push(uint8_t deck, int16_t left, int16_t right)
{
    return timeline_active(deck)
        ? audio_pcm_timeline_push(&s_pcm_timelines[deck], left, right)
        : audio_pcm_ring_push(&s_pcm_rings[deck], left, right);
}

/* Present the immutable canonical store to the existing scratch DSP. This is a
 * metadata-only view; both objects refer to the same interleaved PSRAM frames. */
static void sync_scratch_view_from_timeline(uint8_t deck, uint32_t newest_ms)
{
    if (!timeline_active(deck)) return;
    audio_pcm_timeline_t *t = &s_pcm_timelines[deck];
    audio_scratch_buffer_t *b = &s_scratch_buf[deck];
    b->frames = t->frames;
    b->capacity = t->capacity;
    b->write_index = t->write_index;
    b->filled = audio_pcm_timeline_used_frames(t);
    b->generation = audio_pcm_timeline_generation(t);
    b->newest_pos_ms = newest_ms;
    b->newest_valid = b->filled > 0u;
}

static inline void atomic_store_bool(bool *value, bool new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELEASE);
}

static uint32_t float_to_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits_to_float(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void scratch_head_publish(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return;
    __atomic_store_n(&s_scratch_head_back_bits[deck],
                     float_to_bits(audio_scratch_head_back(&s_scratch_engine[deck])),
                     __ATOMIC_RELEASE);
}

static float scratch_head_snapshot(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return 0.0f;
    return bits_to_float(__atomic_load_n(&s_scratch_head_back_bits[deck],
                                        __ATOMIC_ACQUIRE));
}

/* Scratch release-handoff state (4b) is written by the control task (arming the
 * fade in scratch_end) and read+advanced by the output task per sample. Publish
 * the handoff phase with release/acquire so a reader that observes a new phase
 * also observes the matching s_scratch_handoff_gain seed written just before it
 * (avoids a one-block wrong-source/wrong-gain glitch on the phase change). */
static inline uint8_t scratch_handoff_load(const uint8_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline void scratch_handoff_store(uint8_t *value, uint8_t new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELEASE);
}

#define AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS 1000u
#define AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE 48000u
#define AUDIO_ENGINE_PAD_FX_ECHO_MAX_DELAY_MS 1000u
#define AUDIO_ENGINE_PAD_FX_ECHO_FALLBACK_SAMPLE_RATE 48000u

static void apply_deck_filter_raw(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return;
    uint16_t raw = atomic_load_u16(&s_deck_filter_raw[deck]);
    uint16_t effective = atomic_load_bool(&s_smart_cfx_enabled) ? audio_smart_cfx_curve_raw(raw) : raw;
    atomic_store_u16(&s_deck_filter_effective[deck], effective);
    audio_filter_set_raw(&s_deck_filter[deck], effective);
}

static void apply_all_deck_filter_raw(void)
{
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        apply_deck_filter_raw(deck);
    }
}

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


static uint16_t sample_abs_u16(int16_t sample)
{
    return sample == INT16_MIN ? 32768u : (uint16_t)(sample < 0 ? -sample : sample);
}

static uint16_t frame_peak(audio_mixer_frame_t frame)
{
    uint16_t left = sample_abs_u16(frame.left);
    uint16_t right = sample_abs_u16(frame.right);
    return left > right ? left : right;
}

/* VU meter reference sensitivity: a peak this far below digital full scale reads
 * as a full meter, so normal (non-brickwalled) material lights more than a
 * sliver. ~-6 dBFS. Tune here if the controller/on-screen VU reads hot or cold. */
#define AE_VU_SENSITIVITY 2.0f

/* Pre-fader channel-meter peak: the deck frame is measured BEFORE the channel
 * gain (pregain/trim/volume/crossfader) is mixed into the master, so scale it by
 * the pre-fader gain (pregain x master trim) here. This makes the VU track the
 * TRIM knob (which sets pregain) like a real DJ channel meter, independent of
 * the fader/crossfader. */
static uint16_t frame_peak_prefader(audio_mixer_frame_t frame, float prefader_gain)
{
    float peak = (float)frame_peak(frame) * prefader_gain * AE_VU_SENSITIVITY;
    if (peak < 0.0f) {
        peak = 0.0f;
    }
    if (peak > 32768.0f) {
        peak = 32768.0f;
    }
    return (uint16_t)(peak + 0.5f);
}

static void record_deck_peak_value(uint8_t deck, uint16_t peak)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return;
    /* Raw running max, drained (read-and-reset) by the FLX4 LED path. Atomic so
     * the snapshot + LED reader never need the audio mutex. */
    if (peak > atomic_load_u16(&s_deck_peak[deck])) {
        atomic_store_u16(&s_deck_peak[deck], peak);
    }
}

/* Display VU peak: instant attack, gentle per-block decay (~1/16 per 256-frame
 * block). Maintained only by the output task and read non-destructively by the
 * UI + web snapshot, so it never sticks like the raw read-and-reset peak and is
 * independent of the FLX4 LED consumer. */
static uint16_t vu_decay_peak(uint16_t current, uint16_t block_peak)
{
    uint16_t step = current >> 4;
    if (step == 0) {
        step = 1;   /* guarantee decay reaches 0, not a floor of ~15 that would
                     * keep the bottom VU segment lit after playback stops */
    }
    uint16_t decayed = current > step ? (uint16_t)(current - step) : 0u;
    return block_peak > decayed ? block_peak : decayed;
}

static void record_deck_ui_peak(uint8_t deck, uint16_t block_peak)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return;
    atomic_store_u16(&s_deck_ui_peak[deck],
                     vu_decay_peak(atomic_load_u16(&s_deck_ui_peak[deck]), block_peak));
}

#if defined(AUDIO_ENGINE_PC_TEST)
static void record_deck_peak(uint8_t deck, audio_mixer_frame_t frame)
{
    record_deck_peak_value(deck, frame_peak(frame));
}
#endif


#if AE_FW
static esp_codec_dev_handle_t s_codec       = NULL;  /* owned by bsp_jc4880 */
static i2s_chan_handle_t      s_main_i2s_tx = NULL;  /* optional PCM5102A MAIN OUT */
/* Per-deck counting semaphore: each of a deck's tasks gives on exit. Per-deck
 * (not shared) so tearing down deck A never consumes the exit signals a
 * concurrent load of deck B is waiting on. */
static SemaphoreHandle_t      s_tasks_done[AUDIO_ENGINE_DECK_COUNT] = { NULL };
static SemaphoreHandle_t      s_output_done = NULL;
static TaskHandle_t           s_output_task = NULL;
static volatile bool          s_output_run = false;
/* Guards the decode-task ring/resampler flush against the output-task consumer:
 * both are pinned to AE_AUDIO_TASK_CORE, so a brief critical section makes the
 * two-index ring reset atomic w.r.t. a concurrent pop (which runs outside
 * AE_LOCK during mixing). */
static portMUX_TYPE           s_ring_flush_mux = portMUX_INITIALIZER_UNLOCKED;
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

static audio_resampler_state_t *resampler_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_resamplers[deck];
    }
    return &s_resamplers[AUDIO_ENGINE_COMPAT_DECK];
}

static void apply_pending_pitch(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT ||
        !__atomic_exchange_n(&s_pending_pitch_valid[deck], false,
                             __ATOMIC_ACQ_REL)) {
        return;
    }
    s_engines[deck].pitch_factor = s_pending_pitch_factor[deck];
    audio_resampler_reset(&s_resamplers[deck]);
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
    /* Platter-hold silences the deck and freezes its position (the mixer skips an
     * inactive deck, so it neither outputs nor pops/advances the ring). */
    if (atomic_load_bool(&s_deck_hold[deck])) return false;
    if (atomic_load_bool(&s_scratch_abort_seek_waiting[deck])) return false;
    if (atomic_load_bool(&s_scratch_playing[deck]) &&
        (atomic_load_bool(&s_scratch_started_paused[deck]) ||
         (eng->playing && !eng->paused))) return true;
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

static uint16_t ae_wav_rd_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

#if AE_FW
static uint32_t ae_wav_rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static esp_err_t ae_wav_init_from_memory(audio_engine_state_t *eng)
{
    if (!eng || !eng->file_buf || eng->file_size < 12u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_format_detect_header(eng->file_buf, eng->file_size) != AUDIO_FORMAT_WAV) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint16_t block_align = 0;
    uint32_t sample_rate = 0;
    size_t data_offset = 0;
    size_t data_size = 0;
    size_t pos = 12u;

    while (pos + 8u <= eng->file_size && !have_data) {
        const uint8_t *chunk = eng->file_buf + pos;
        uint32_t chunk_size = ae_wav_rd_u32le(chunk + 4);
        size_t payload = pos + 8u;
        size_t padded_size = (size_t)chunk_size + (size_t)(chunk_size & 1u);
        if (payload > eng->file_size || padded_size > eng->file_size - payload) {
            return ESP_FAIL;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16u) {
                return ESP_FAIL;
            }
            const uint8_t *fmt = eng->file_buf + payload;
            audio_format = ae_wav_rd_u16le(fmt + 0);
            channels = ae_wav_rd_u16le(fmt + 2);
            sample_rate = ae_wav_rd_u32le(fmt + 4);
            block_align = ae_wav_rd_u16le(fmt + 12);
            bits_per_sample = ae_wav_rd_u16le(fmt + 14);
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_offset = payload;
            data_size = chunk_size;
            have_data = true;
        }

        pos = payload + padded_size;
    }

    if (!have_fmt || !have_data || audio_format != 1u) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((channels != 1u && channels != 2u) || bits_per_sample != 16u) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (sample_rate == 0u ||
        block_align == 0u ||
        block_align != (uint16_t)(channels * sizeof(int16_t))) {
        return ESP_FAIL;
    }

    eng->format = AUDIO_FORMAT_WAV;
    eng->sample_rate = sample_rate;
    eng->channels = (int)channels;
    eng->wav_ready = true;
    eng->wav_data_offset = data_offset;
    eng->wav_data_size = data_size;
    eng->wav_data_pos = data_offset;
    eng->wav_block_align = block_align;
    eng->wav_total_frames = data_size / block_align;
    eng->wav_current_frame = 0u;
    eng->file_pos = data_offset;
    eng->eof = (eng->wav_total_frames == 0u);
    if (eng->duration_ms == 0u) {
        eng->duration_ms = (uint32_t)((eng->wav_total_frames * 1000ull) /
                                      (uint64_t)sample_rate);
    }
    ESP_LOGI(TAG, "WAV: %u Hz, %u ch, %u frames",
             (unsigned)sample_rate,
             (unsigned)channels,
             (unsigned)eng->wav_total_frames);
    return ESP_OK;
}
#endif

static void ae_wav_seek_to_ms(audio_engine_state_t *eng, uint32_t position_ms)
{
    if (!eng || !eng->wav_ready || eng->sample_rate == 0u || eng->wav_block_align == 0u) {
        return;
    }
    uint64_t frame = ((uint64_t)position_ms * (uint64_t)eng->sample_rate) / 1000ull;
    if (frame > eng->wav_total_frames) {
        frame = eng->wav_total_frames;
    }
    eng->wav_current_frame = frame;
    eng->wav_data_pos = eng->wav_data_offset + (size_t)(frame * eng->wav_block_align);
    eng->file_pos = eng->wav_data_pos;
    eng->eof = (frame >= eng->wav_total_frames);
}

static int ae_wav_decode_one_frame(audio_engine_state_t *eng,
                                   int16_t out_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2])
{
    if (!eng || !eng->wav_ready || eng->eof || eng->wav_block_align == 0u) {
        return 0;
    }
    if (eng->wav_current_frame >= eng->wav_total_frames) {
        eng->eof = true;
        return 0;
    }

    uint64_t frames_left64 = eng->wav_total_frames - eng->wav_current_frame;
    size_t frames = frames_left64 > (uint64_t)MINIMP3_MAX_SAMPLES_PER_FRAME
                        ? (size_t)MINIMP3_MAX_SAMPLES_PER_FRAME
                        : (size_t)frames_left64;
    size_t data_end = eng->wav_data_offset + eng->wav_data_size;
    if (eng->wav_data_pos >= data_end) {
        eng->eof = true;
        return 0;
    }
    size_t bytes_left = data_end - eng->wav_data_pos;
    size_t frames_available = bytes_left / eng->wav_block_align;
    if (frames > frames_available) {
        frames = frames_available;
    }
    if (frames == 0u) {
        eng->eof = true;
        return 0;
    }

    for (size_t i = 0; i < frames; i++) {
        const uint8_t *p = eng->file_buf + eng->wav_data_pos + i * eng->wav_block_align;
        if (eng->channels == 1) {
            int16_t s = (int16_t)ae_wav_rd_u16le(p);
            out_pcm[i * 2u + 0u] = s;
            out_pcm[i * 2u + 1u] = s;
        } else {
            out_pcm[i * 2u + 0u] = (int16_t)ae_wav_rd_u16le(p + 0);
            out_pcm[i * 2u + 1u] = (int16_t)ae_wav_rd_u16le(p + 2);
        }
    }

    eng->wav_current_frame += frames;
    eng->wav_data_pos += frames * eng->wav_block_align;
    eng->file_pos = eng->wav_data_pos;
    if (eng->wav_current_frame >= eng->wav_total_frames) {
        eng->eof = true;
    }
    return (int)frames;
}

#if AE_FW
/* ── Firmware FLAC decode over the PSRAM preload buffer (dr_flac) ──────────── *
 *
 * Unlike MP3/WAV the FLAC decoder needs the whole file resident (dr_flac reads
 * STREAMINFO and seeks within the stream), so ae_flac_init_from_memory is
 * called only after the loader signals load_done; it decodes directly from the
 * in-PSRAM buffer with drflac_open_memory (never touches USB during playback).
 */
static void *ae_flac_psram_malloc(size_t sz, void *ud)
{
    (void)ud;
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void *ae_flac_psram_realloc(void *p, size_t sz, void *ud)
{
    (void)ud;
    return heap_caps_realloc(p, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void ae_flac_psram_free(void *p, void *ud)
{
    (void)ud;
    heap_caps_free(p);
}

static esp_err_t ae_flac_init_from_memory(audio_engine_state_t *eng)
{
    if (!eng || !eng->file_buf || eng->file_size == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    drflac_allocation_callbacks cb = {
        .pUserData = NULL,
        .onMalloc = ae_flac_psram_malloc,
        .onRealloc = ae_flac_psram_realloc,
        .onFree = ae_flac_psram_free,
    };
    drflac *flac = drflac_open_memory(eng->file_buf, eng->file_size, &cb);
    if (!flac) {
        ESP_LOGE(TAG, "drflac_open_memory failed (size=%u)", (unsigned)eng->file_size);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (flac->channels != 1u && flac->channels != 2u) {
        ESP_LOGE(TAG, "FLAC unsupported channel count: %u", (unsigned)flac->channels);
        drflac_close(flac);
        return ESP_ERR_NOT_SUPPORTED;
    }

    eng->flac = flac;
    eng->flac_ready = true;
    eng->format = AUDIO_FORMAT_FLAC;
    eng->sample_rate = flac->sampleRate;
    eng->channels = (int)flac->channels;
    if (eng->duration_ms == 0u && flac->sampleRate > 0u) {
        eng->duration_ms = (uint32_t)((flac->totalPCMFrameCount * 1000ull) /
                                      (uint64_t)flac->sampleRate);
    }
    eng->eof = (flac->totalPCMFrameCount == 0u);
    ESP_LOGI(TAG, "FLAC: %u Hz, %u ch, %u bps, %llu frames",
             (unsigned)flac->sampleRate,
             (unsigned)flac->channels,
             (unsigned)flac->bitsPerSample,
             (unsigned long long)flac->totalPCMFrameCount);
    return ESP_OK;
}

static void ae_flac_seek_to_ms(audio_engine_state_t *eng, uint32_t position_ms)
{
    if (!eng || !eng->flac_ready || !eng->flac || eng->sample_rate == 0u) {
        return;
    }
    drflac *flac = (drflac *)eng->flac;
    uint64_t frame = ((uint64_t)position_ms * (uint64_t)eng->sample_rate) / 1000ull;
    if (frame > flac->totalPCMFrameCount) {
        frame = flac->totalPCMFrameCount;
    }
    (void)drflac_seek_to_pcm_frame(flac, (drflac_uint64)frame);
    eng->eof = (frame >= flac->totalPCMFrameCount);
}

static int ae_flac_decode_one_frame(audio_engine_state_t *eng,
                                    int16_t out_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2])
{
    if (!eng || !eng->flac_ready || !eng->flac || eng->eof) {
        return 0;
    }
    drflac *flac = (drflac *)eng->flac;
    const uint8_t channels = (uint8_t)eng->channels;

    /* dr_flac interleaves native channels; decode into a scratch and pack to
     * stereo. MINIMP3_MAX_SAMPLES_PER_FRAME frames per call keeps the ring fed
     * at the same cadence as the MP3/WAV paths. */
    int16_t scratch[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    drflac_uint64 got = drflac_read_pcm_frames_s16(flac,
                                                   MINIMP3_MAX_SAMPLES_PER_FRAME,
                                                   scratch);
    if (got == 0u) {
        eng->eof = true;
        return 0;
    }
    for (size_t i = 0; i < (size_t)got; ++i) {
        if (channels == 1u) {
            int16_t s = scratch[i];
            out_pcm[i * 2u + 0u] = s;
            out_pcm[i * 2u + 1u] = s;
        } else {
            out_pcm[i * 2u + 0u] = scratch[i * channels + 0u];
            out_pcm[i * 2u + 1u] = scratch[i * channels + 1u];
        }
    }
    return (int)got;
}
#endif /* AE_FW */

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═════════════════════════════════════════════════════════════════════════ */

/*
 * decode_one_frame — read + decode one MP3 frame from a deck engine.
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
    if (eng->decoder_open) {
        if (eng->eof) return 0;
        size_t frames_read = 0;
        esp_err_t rc = audio_decoder_read_pcm_s16(&eng->decoder,
                                                  out_pcm,
                                                  MINIMP3_MAX_SAMPLES_PER_FRAME,
                                                  &frames_read);
        if (rc != ESP_OK || frames_read == 0u) {
            eng->eof = true;
            return 0;
        }
        if (eng->sample_rate == 0u && eng->decoder.info.sample_rate > 0u) {
            eng->sample_rate = eng->decoder.info.sample_rate;
            eng->channels = eng->decoder.info.channels;
        }
        return (int)frames_read;
    }

    if (eng->format == AUDIO_FORMAT_WAV) {
        return ae_wav_decode_one_frame(eng, out_pcm);
    }
#if AE_FW
    if (eng->format == AUDIO_FORMAT_FLAC) {
        return ae_flac_decode_one_frame(eng, out_pcm);
    }
#endif

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
#if AE_FW
static void build_seek_table(audio_engine_state_t *eng)
{
    if (!eng->file_buf && !eng->fp) return;

    uint32_t cap = 20000; /* initial capacity for ~8-10 mins track */
#if AE_FW
    uint32_t *seek_table = heap_caps_malloc(cap * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
#else
    uint32_t *seek_table = malloc(cap * sizeof(uint32_t));
#endif
    if (!seek_table) {
        ESP_LOGE(TAG, "Failed to allocate seek table memory!");
        return;
    }
    uint32_t seek_table_len = 0;

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
#if AE_FW
        heap_caps_free(seek_table);
#else
        free(seek_table);
#endif
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

            if (seek_table_len >= cap) {
                cap *= 2;
#if AE_FW
                uint32_t *new_table = heap_caps_realloc(seek_table, cap * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
#else
                uint32_t *new_table = realloc(seek_table, cap * sizeof(uint32_t));
#endif
                if (!new_table) {
                    ESP_LOGE(TAG, "Failed to reallocate seek table!");
                    break;
                }
                seek_table = new_table;
            }

            seek_table[seek_table_len++] = (uint32_t)pos;
            pos += frame_bytes;
        } else {
            pos++;
        }
    }

    if (scan_buf) {
        free(scan_buf);
    }

    int64_t dt_us = ae_now_us() - t0;
    AE_LOCK();
    uint32_t *old_table = eng->seek_table;
    eng->seek_table = seek_table;
    eng->seek_table_len = seek_table_len;
    AE_UNLOCK();
    if (old_table) {
#if AE_FW
        heap_caps_free(old_table);
#else
        free(old_table);
#endif
    }
    ESP_LOGI(TAG, "Indexed %u MP3 frames in %lld ms", 
             (unsigned)seek_table_len, (long long)(dt_us / 1000));
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
#endif /* AE_FW */



/* ── Firmware decode + I2S output tasks (ESP32-P4) ────────────────────────── */
#if AE_FW
/* The producer is split into a loader + a decoder (P5b progressive preload):
 * the loader streams the file from USB into PSRAM while the decoder plays from
 * the already-loaded region. Only the loader ever touches USB, so there is never
 * a concurrent USB transfer (the condition that crashed usb_dwc_hal). This also
 * cuts load-to-play latency: playback starts after the first chunk (~0.25 s)
 * instead of waiting for the whole file (USB read is only ~1 MB/s).
 * `s_decode_pcm` is per-deck and static (9 KB/deck) to keep it off the task
 * stack without sharing decoded samples between concurrent deck decoders. */
static int16_t s_decode_pcm[AUDIO_ENGINE_DECK_COUNT][MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

#define AE_FIRST_CHUNK_BYTES (96u * 1024u)  /* min loaded before the decoder starts */
#define AE_LOAD_GATE_MARGIN  (32u * 1024u)  /* keep the decoder this far behind the loader */
#define AE_DIAG_OUTPUT_REPORT_BLOCKS 300u
#define AE_DIAG_DECODE_REPORT_FRAMES 120u
#define AE_DIAG_PRELOAD_REPORT_CHUNKS 64u

static audio_diag_counter_t s_diag_output_blocks;
static audio_diag_late_counter_t s_diag_output_late;
static audio_diag_counter_t s_diag_decode_frames[AUDIO_ENGINE_DECK_COUNT];
static audio_diag_counter_t s_diag_preload_chunks[AUDIO_ENGINE_DECK_COUNT];

static esp_err_t audio_output_service_open_codec(uint32_t sample_rate);
static esp_err_t audio_output_service_ensure_started(void);
static esp_err_t audio_output_service_stop(void);

static void ae_diag_reset(void)
{
    audio_diag_counter_init(&s_diag_output_blocks, AE_DIAG_OUTPUT_REPORT_BLOCKS);
    audio_diag_late_counter_init(&s_diag_output_late, 1u);
    s_limiter_stats = (audio_mixer_limiter_stats_t){ 0 };
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        audio_diag_counter_init(&s_diag_decode_frames[deck], AE_DIAG_DECODE_REPORT_FRAMES);
        audio_diag_counter_init(&s_diag_preload_chunks[deck], AE_DIAG_PRELOAD_REPORT_CHUNKS);
    }
}

static void ae_diag_log_memory(const char *phase, uint8_t deck)
{
    ESP_LOGI(TAG,
             "diag %s D%u: heap=%u internal=%u psram=%u",
             phase ? phase : "mem",
             (unsigned)(deck + 1u),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void ae_diag_record_decode(uint8_t deck,
                                  uint32_t decode_us,
                                  int samples,
                                  uint32_t ring_used,
                                  size_t file_pos,
                                  size_t loaded_bytes,
                                  bool load_done)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT || samples <= 0) {
        return;
    }

    audio_diag_report_t report;
    if (audio_diag_record(&s_diag_decode_frames[deck], decode_us, &report)) {
        ESP_LOGI(TAG,
                 "diag decode D%u: last=%u us avg=%u us max=%u us samples=%u ring=%u/%u file=%u loaded=%u done=%u",
                 (unsigned)(deck + 1u),
                 (unsigned)report.last_us,
                 (unsigned)report.avg_us,
                 (unsigned)report.max_us,
                 (unsigned)report.samples,
                 (unsigned)ring_used,
                 (unsigned)AUDIO_PCM_RING_FRAMES,
                 (unsigned)file_pos,
                 (unsigned)loaded_bytes,
                 load_done ? 1u : 0u);
    }
}

static void ae_diag_record_preload_chunk(uint8_t deck,
                                         uint32_t chunk_us,
                                         size_t got,
                                         size_t off,
                                         size_t total)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT || got == 0) {
        return;
    }
    audio_diag_report_t report;
    if (audio_diag_record(&s_diag_preload_chunks[deck], chunk_us, &report)) {
        ESP_LOGI(TAG,
                 "diag preload D%u: last=%u us avg=%u us max=%u us chunks=%u bytes=%u off=%u/%u heap=%u psram=%u",
                 (unsigned)(deck + 1u),
                 (unsigned)report.last_us,
                 (unsigned)report.avg_us,
                 (unsigned)report.max_us,
                 (unsigned)report.samples,
                 (unsigned)got,
                 (unsigned)off,
                 (unsigned)total,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

static void ae_diag_record_output_block(uint32_t block_us,
                                        uint32_t late_threshold_us,
                                        uint32_t consumed0,
                                        uint32_t consumed1,
                                        bool active0,
                                        bool active1,
                                        const audio_mixer_limiter_stats_t *limiter_stats)
{
    audio_diag_report_t report;
    if (audio_diag_record(&s_diag_output_blocks, block_us, &report)) {
        ESP_LOGI(TAG,
                 "diag output: last=%u us avg=%u us max=%u us samples=%u active=%u/%u consumed=%u/%u future=%u/%u history=%u/%u underrun=%u/%u edge=%u/%u limiter=%u +%u -%u peak=%d late=%u late_max=%u us heap=%u internal=%u psram=%u",
                 (unsigned)report.last_us,
                 (unsigned)report.avg_us,
                 (unsigned)report.max_us,
                 (unsigned)report.samples,
                 active0 ? 1u : 0u,
                 active1 ? 1u : 0u,
                 (unsigned)consumed0,
                 (unsigned)consumed1,
                 (unsigned)deck_pcm_used(0u),
                 (unsigned)deck_pcm_used(1u),
                 (unsigned)(timeline_active(0u) ? audio_pcm_timeline_history_frames(&s_pcm_timelines[0]) : 0u),
                 (unsigned)(timeline_active(1u) ? audio_pcm_timeline_history_frames(&s_pcm_timelines[1]) : 0u),
                 (unsigned)s_pcm_underrun_count[0],
                 (unsigned)s_pcm_underrun_count[1],
                 (unsigned)s_scratch_engine[0].edge_hits,
                 (unsigned)s_scratch_engine[1].edge_hits,
                 limiter_stats ? (unsigned)limiter_stats->limited_samples : 0u,
                 limiter_stats ? (unsigned)limiter_stats->positive_overloads : 0u,
                 limiter_stats ? (unsigned)limiter_stats->negative_overloads : 0u,
                 limiter_stats ? (int)limiter_stats->peak_input_abs : 0,
                 (unsigned)s_diag_output_late.count,
                 (unsigned)s_diag_output_late.max_us,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    if (late_threshold_us > 0) {
        s_diag_output_late.threshold_us = late_threshold_us;
    }
    if (late_threshold_us > 0 && audio_diag_late_record(&s_diag_output_late, block_us)) {
        ESP_LOGW(TAG,
                 "diag output late: block=%u us threshold=%u us active=%u/%u pcm_future=%u/%u %u/%u history=%u/%u late_count=%u late_max=%u us",
                 (unsigned)block_us,
                 (unsigned)late_threshold_us,
                 active0 ? 1u : 0u,
                 active1 ? 1u : 0u,
                 (unsigned)deck_pcm_used(0u),
                 (unsigned)(timeline_active(0u) ? AE_TIMELINE_CAPACITY_FRAMES : AUDIO_PCM_RING_FRAMES),
                 (unsigned)deck_pcm_used(1u),
                 (unsigned)(timeline_active(1u) ? AE_TIMELINE_CAPACITY_FRAMES : AUDIO_PCM_RING_FRAMES),
                 (unsigned)(timeline_active(0u) ? audio_pcm_timeline_history_frames(&s_pcm_timelines[0]) : 0u),
                 (unsigned)(timeline_active(1u) ? audio_pcm_timeline_history_frames(&s_pcm_timelines[1]) : 0u),
                 (unsigned)s_diag_output_late.count,
                 (unsigned)s_diag_output_late.max_us);
    }
}

static void ae_fail_load(audio_engine_state_t *eng,
                         audio_fw_preload_t *fw,
                         audio_fw_runtime_t *runtime,
                         esp_err_t err,
                         const char *err_text)
{
    if (eng) {
        eng->last_error = err;
        snprintf(eng->last_error_text,
                 sizeof(eng->last_error_text),
                 "%s",
                 err_text ? err_text : "LOAD ERR");
        eng->loading = false;
        eng->load_progress = 100;
        eng->loaded = false;
        eng->playing = false;
        eng->paused = false;
    }
    audio_fw_preload_abort_load(fw, runtime);
}

/* The per-deck exit semaphore for a task's context. ctx->deck is bound before
 * the task is created, so it is always valid here; clamp defensively. */
static SemaphoreHandle_t ctx_tasks_done(const audio_fw_task_context_t *ctx)
{
    uint8_t d = (ctx && ctx->deck < AUDIO_ENGINE_DECK_COUNT) ? ctx->deck : 0u;
    return s_tasks_done[d];
}

/* Loader: read the MP3 from USB into PSRAM in chunks, publishing the watermark;
 * build the frame seek table once the whole file is in. Parks
 * until stop() so the teardown counting semaphore stays balanced. */
static void ae_loader_task(void *arg)
{
    audio_fw_task_context_t *ctx = (audio_fw_task_context_t *)arg;
    if (!audio_fw_task_context_is_bound(ctx)) {
        xSemaphoreGive(ctx_tasks_done(ctx));
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
        ae_fail_load(eng, fw, runtime, ESP_ERR_NOT_FOUND, "NOT FOUND");
        goto park;
    }
    fseek(src, 0, SEEK_END);
    long fsz = ftell(src);
    fseek(src, 0, SEEK_SET);
    if (fsz <= 0) {
        ESP_LOGE(TAG, "bad size %ld: %s", fsz, fw->path);
        fclose(src);
        media_io_gate_end();
        ae_fail_load(eng, fw, runtime, ESP_ERR_INVALID_SIZE, "BAD SIZE");
        goto park;
    }

    fw->buf = heap_caps_malloc((size_t)fsz, MALLOC_CAP_SPIRAM);
    if (!fw->buf) {
        ESP_LOGE(TAG, "PSRAM alloc %ld B failed", fsz);
        fclose(src);
        media_io_gate_end();
        ae_fail_load(eng, fw, runtime, ESP_ERR_NO_MEM, "NO MEM");
        goto park;
    }
    ae_diag_log_memory("preload-alloc", ctx->deck);

    AE_LOCK();
    eng->file_buf  = fw->buf;
    eng->file_size = (size_t)fsz;
    eng->file_pos  = 0;
    eng->fp        = NULL;
    AE_UNLOCK();

    int64_t t0  = esp_timer_get_time();
    size_t  off = 0;
    while (off < (size_t)fsz && runtime->run && media_io_gate_is_available()) {
        size_t want = audio_fw_preload_chunk_bytes((size_t)fsz - off,
                                                   audio_fw_output_task_running());
        int64_t chunk_start_us = esp_timer_get_time();
        size_t got = fread(fw->buf + off, 1, want, src);
        uint32_t chunk_us = (uint32_t)(esp_timer_get_time() - chunk_start_us);
        if (got == 0) {
            /* READ10 may fail just before the MSC callback publishes media
             * loss.  Allow one scheduler window before classifying the short
             * read as a genuine preload error. */
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
        off += got;
        fw->loaded_bytes = off;                               /* publish watermark */
        eng->load_progress = (uint8_t)(off * 100u / (size_t)fsz);
        ae_diag_record_preload_chunk(ctx->deck, chunk_us, got, off, (size_t)fsz);
    }
    fclose(src);
    media_io_gate_end();

    if (runtime->run && media_io_gate_is_available() && off != (size_t)fsz) {
        ESP_LOGE(TAG, "preload INCOMPLETE D%u: %u/%u bytes",
                 (unsigned)ctx->deck, (unsigned)off, (unsigned)fsz);
        ae_fail_load(eng, fw, runtime, ESP_ERR_INVALID_SIZE, "PRELOAD ERR");
    }
    if (runtime->run && media_io_gate_is_available() && off == (size_t)fsz) {
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        ESP_LOGI(TAG, "preloaded %u KB in %lld ms (%.1f MB/s)", (unsigned)(off / 1024u),
                 (long long)dt_ms, dt_ms > 0 ? (off / 1048576.0) / (dt_ms / 1000.0) : 0.0);
        ae_diag_log_memory("preload-done", ctx->deck);
        if (eng->format != AUDIO_FORMAT_WAV && eng->format != AUDIO_FORMAT_FLAC) {
            build_seek_table(eng);    /* MP3 only: WAV/FLAC seek by frame index */
        }
        fw->load_done = true;
        eng->load_progress = 100;
    }

park:
    while (runtime->run) vTaskDelay(pdMS_TO_TICKS(20));   /* stay alive until stop() */
    runtime->loader_task = NULL;
    xSemaphoreGive(ctx_tasks_done(ctx));
    vTaskDelete(NULL);
}

/* Decoder: plays from the loaded PSRAM region; never touches USB. */
static void ae_decode_task(void *arg)
{
    audio_fw_task_context_t *ctx = (audio_fw_task_context_t *)arg;
    if (!audio_fw_task_context_is_bound(ctx)) {
        xSemaphoreGive(ctx_tasks_done(ctx));
        vTaskDeleteWithCaps(NULL);
        return;
    }
    audio_fw_preload_t *fw = ctx->preload;
    audio_fw_runtime_t *runtime = ctx->runtime;
    audio_engine_state_t *eng = (audio_engine_state_t *)ctx->engine;
    audio_scratch_buffer_t *scratch = scratch_buffer_for_deck(ctx->deck);
    audio_resampler_state_t *resampler = (audio_resampler_state_t *)ctx->resampler;
    if (ctx->deck >= AUDIO_ENGINE_DECK_COUNT) {
        xSemaphoreGive(ctx_tasks_done(ctx));
        vTaskDeleteWithCaps(NULL);
        return;
    }
    int16_t *decode_pcm = s_decode_pcm[ctx->deck];
    bool scratch_full_logged = false;  /* one-shot fill diagnostic (Phase 2) */

    /* Wait for the loader to allocate the buffer and fetch the first chunk. */
    while (runtime->run && fw->loaded_bytes < AE_FIRST_CHUNK_BYTES && !fw->load_done) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!runtime->run) goto cleanup;

    if (eng->format == AUDIO_FORMAT_WAV || eng->format == AUDIO_FORMAT_FLAC) {
        /* WAV and FLAC both decode from the fully-loaded PSRAM buffer. */
        const bool is_wav = (eng->format == AUDIO_FORMAT_WAV);
        while (runtime->run && !fw->load_done) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (!runtime->run) goto cleanup;
        AE_LOCK();
        esp_err_t init_rc = is_wav ? ae_wav_init_from_memory(eng)
                                   : ae_flac_init_from_memory(eng);
        AE_UNLOCK();
        if (init_rc != ESP_OK) {
            ESP_LOGE(TAG, "%s parse failed: %d", is_wav ? "WAV" : "FLAC", (int)init_rc);
            ae_fail_load(eng, fw, runtime, init_rc, is_wav ? "WAV ERR" : "FLAC ERR");
            goto cleanup;
        }
    } else {
        /* Latch the sample rate from the first decodable frame, then open the codec.
         * Gated: a large ID3 tag may push frame 1 past the first chunk - wait for it. */
        int attempts = 0;
        while (runtime->run && eng->sample_rate == 0 && attempts < 256 && !eng->eof) {
            if (!fw->load_done && eng->file_pos + AE_LOAD_GATE_MARGIN > fw->loaded_bytes) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            AE_LOCK();
            int64_t decode_start_us = esp_timer_get_time();
            int n = decode_one_frame(eng, fw, decode_pcm);
            uint32_t decode_us = (uint32_t)(esp_timer_get_time() - decode_start_us);
            size_t file_pos = eng->file_pos;
            if (n > 0) {
                eng->frames_since_seek += (uint64_t)n;
                for (int i = 0; i < n; i++) {
                    (void)deck_pcm_push(ctx->deck, decode_pcm[i * 2], decode_pcm[i * 2 + 1]);
                }
            }
            AE_UNLOCK();
            /* Diagnostics (its periodic ESP_LOGI does blocking UART I/O) run
             * outside the lock; the ring used-count is an atomic SPSC read. */
            if (n > 0) {
                ae_diag_record_decode(ctx->deck,
                                      decode_us,
                                      n,
                                      deck_pcm_used(ctx->deck),
                                      file_pos,
                                      fw->loaded_bytes,
                                      fw->load_done);
            }
            attempts++;
        }
    }
    if (!runtime->run) {
        goto cleanup;
    }
    if (eng->sample_rate == 0) {
        ESP_LOGE(TAG, "no audio frame found");
        ae_fail_load(eng, fw, runtime, ESP_FAIL, "NO AUDIO FRAME");
        goto cleanup;
    }

    /* The I2S/codec output path targets 44.1/48 kHz; hi-res sources (96/192 kHz
     * FLAC) are downsampled by the per-deck output resampler, so the codec opens
     * at a supported rate while the deck keeps its native source rate. */
    uint32_t codec_rate = eng->sample_rate > 48000u ? 48000u : eng->sample_rate;
    if (audio_output_service_open_codec(codec_rate) != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open(%u Hz) failed", (unsigned)codec_rate);
        ae_fail_load(eng, fw, runtime, ESP_FAIL, "CODEC OPEN ERR");
        goto cleanup;
    }
    if (codec_rate != eng->sample_rate) {
        ESP_LOGI(TAG, "hi-res source %u Hz D%u -> output %u Hz (resampled)",
                 (unsigned)eng->sample_rate, (unsigned)ctx->deck, (unsigned)codec_rate);
    }
    ESP_LOGI(TAG,
             "track format D%u: %u Hz %d ch file=%u bytes loaded=%u done=%u",
             (unsigned)ctx->deck,
             (unsigned)eng->sample_rate,
             eng->channels,
             (unsigned)eng->file_size,
             (unsigned)fw->loaded_bytes,
             fw->load_done ? 1u : 0u);
    ESP_LOGI(TAG, "producer ready @ %u Hz, shared output mixer eligible", (unsigned)eng->sample_rate);
    eng->load_progress = 100;
    eng->loading       = false;   /* P5a: track is now playable */

    /* Scratch capture (vinyl Phase 2): the source rate is now known, so bind it
     * for ms<->frame mapping and start the window fresh. */
    audio_scratch_buffer_set_sample_rate(scratch, eng->sample_rate);
    audio_scratch_buffer_reset(scratch);
    if (timeline_active(ctx->deck)) {
        sync_scratch_view_from_timeline(ctx->deck, 0u);
    }

    /* Steady-state decode loop (reads from PSRAM memory — no USB). */
    while (runtime->run) {
        if (eng->seek_requested) {
            AE_LOCK();
            if (eng->seek_requested) {
                uint32_t target_ms = eng->seek_target_ms;
                ae_seek_reason_t seek_reason = (ae_seek_reason_t)eng->seek_reason;
                bool loop_seek = seek_reason == AE_SEEK_REASON_LOOP;
                bool cue_preroll = timeline_active(ctx->deck) &&
                    seek_reason == AE_SEEK_REASON_USER &&
                    !eng->playing && eng->sample_rate > 0u && target_ms > 0u;
                uint32_t decode_target_ms = target_ms;
                if (cue_preroll) {
                    uint32_t pre_ms = target_ms < AE_TIMELINE_FORWARD_MS
                        ? target_ms : AE_TIMELINE_FORWARD_MS;
                    decode_target_ms = target_ms - pre_ms;
                    eng->timeline_preroll_frames =
                        (uint32_t)(((uint64_t)pre_ms * eng->sample_rate) / 1000u);
                    eng->timeline_preroll_pending =
                        eng->timeline_preroll_frames > 0u;
                } else {
                    eng->timeline_preroll_frames = 0u;
                    eng->timeline_preroll_pending = false;
                }
                if (eng->format == AUDIO_FORMAT_WAV) {
                    ae_wav_seek_to_ms(eng, decode_target_ms);
                } else if (eng->format == AUDIO_FORMAT_FLAC) {
                    ae_flac_seek_to_ms(eng, decode_target_ms);
                } else if (eng->seek_table) {
                    seek_index(eng, decode_target_ms);
                } else if (eng->has_pvbr) {
                    seek_pvbr(eng, decode_target_ms);
                } else {
                    seek_estimate(eng, decode_target_ms);
                }
                eng->seek_base_ms      = decode_target_ms;
                eng->frames_since_seek = 0u;
                if (!loop_seek) {
                    eng->output_base_ms = target_ms;
                    eng->output_frames_since_seek = 0u;
                }
                eng->eof               = false;
                if (eng->format != AUDIO_FORMAT_WAV && eng->format != AUDIO_FORMAT_FLAC) {
                    mp3dec_init(&eng->dec);
                }

                /* Loop wrap keeps the ring (gapless); user seeks flush it.
                 * The output task pops the ring / reads the resampler without
                 * AE_LOCK, so shut out preemption on this core while both are
                 * reset — otherwise a pop interleaved with the two-index reset
                 * sees a bogus (underflowed) used count and streams garbage. */
                if (!loop_seek) {
                    taskENTER_CRITICAL(&s_ring_flush_mux);
                    deck_pcm_reset(ctx->deck);
                    audio_resampler_reset(resampler);
                    taskEXIT_CRITICAL(&s_ring_flush_mux);
                    /* A user seek is a position discontinuity: drop the captured
                     * window so the contiguous-frames assumption stays valid.
                     * Exception: the release-handoff seek keeps s_scratch_playing
                     * set — leave the window intact so the fade-out (4b) can keep
                     * reading it; capture resumes (and refills) once handoff ends. */
                    if (!timeline_active(ctx->deck) &&
                        seek_reason != AE_SEEK_REASON_SCRATCH_RELEASE) {
                        audio_scratch_buffer_reset(scratch);
                    }
                } else if (!timeline_active(ctx->deck) &&
                           !atomic_load_bool(&s_scratch_playing[ctx->deck])) {
                    /* Gapless ring playback survives loop wrap, but capture does
                     * not: loop-end -> loop-start is a new PCM generation. */
                    audio_scratch_buffer_reset(scratch);
                }
                if (seek_reason == AE_SEEK_REASON_SCRATCH_ABORT) {
                    atomic_store_bool(&s_scratch_abort_seek_waiting[ctx->deck], false);
                }
                eng->seek_reason    = AE_SEEK_REASON_USER;
                eng->seek_requested = false;
            }
            AE_UNLOCK();
        }

        /* Gate: never decode past what the loader has fetched into PSRAM.
         * WAV and FLAC only start after load_done, so the gate is MP3-only. */
        if (eng->format != AUDIO_FORMAT_WAV &&
            eng->format != AUDIO_FORMAT_FLAC &&
            !fw->load_done &&
            eng->file_pos + AE_LOAD_GATE_MARGIN > fw->loaded_bytes) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* Canonical mode freezes the producer for the complete scratch gesture;
         * the already-decoded future remains readable and immutable. */
        if (timeline_active(ctx->deck) &&
            (atomic_load_bool(&s_scratch_capture_freeze[ctx->deck]) ||
             atomic_load_bool(&s_scratch_playing[ctx->deck]))) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (eng->eof ||
            deck_pcm_free(ctx->deck, eng->sample_rate) <
                (uint32_t)MINIMP3_MAX_SAMPLES_PER_FRAME) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        uint32_t scratch_newest_ms = 0u;
        bool scratch_newest_valid = false;
        AE_LOCK();
        int64_t decode_start_us = esp_timer_get_time();
        int  samples = decode_one_frame(eng, fw, decode_pcm);
        uint32_t decode_us = (uint32_t)(esp_timer_get_time() - decode_start_us);
        if (samples > 0) {
            eng->frames_since_seek += (uint64_t)samples;
            /* Source position of this batch's last frame, for scratch capture
             * tagging (Phase 2). Same timeline as the playhead (output_base_ms). */
            if (eng->sample_rate > 0u) {
                scratch_newest_ms = eng->seek_base_ms +
                    (uint32_t)(((eng->frames_since_seek - 1u) * 1000ull) / eng->sample_rate);
                scratch_newest_valid = true;
            }
            if (eng->loop_active && eng->sample_rate > 0) {
                uint32_t current_ms = eng->seek_base_ms + (uint32_t)(eng->frames_since_seek * 1000u / eng->sample_rate);
                if (current_ms >= eng->loop_end_ms) {
                    eng->seek_target_ms = eng->loop_start_ms;
                    eng->seek_reason    = AE_SEEK_REASON_LOOP; /* gapless ring */
                    eng->seek_requested = true;
                }
            }
        }
        bool eof = eng->eof;
        size_t file_pos = eng->file_pos;
        AE_UNLOCK();

        if (eof && samples <= 0) {
            eng->playing = false;
            while (eng->eof && runtime->run) vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (samples <= 0) continue;

        ae_diag_record_decode(ctx->deck,
                              decode_us,
                              samples,
                              deck_pcm_used(ctx->deck),
                              file_pos,
                              fw->loaded_bytes,
                              fw->load_done);

        /* Once scratch fade-out has released the frozen history, drop it before
         * capture can resume at the release-seek target. */
        if (__atomic_exchange_n(&s_scratch_capture_reset_ready[ctx->deck], false,
                                __ATOMIC_ACQ_REL)) {
            if (!timeline_active(ctx->deck)) audio_scratch_buffer_reset(scratch);
        }

        /* Freeze scratch capture while this deck is scratching so the window's
         * newest frame stays put under the jog-driven read head (the head is
         * measured as frames-back-from-newest). Ring capture continues so normal
         * playback can resume on release (which seeks + flushes anyway). */
        bool capture_scratch =
            !atomic_load_bool(&s_scratch_playing[ctx->deck]) &&
            !atomic_load_bool(&s_scratch_capture_freeze[ctx->deck]);
        if (capture_scratch) {
            atomic_store_bool(&s_scratch_capture_writing[ctx->deck], true);
            /* Close the check->writer-active race with scratch_begin: once the
             * writer flag is visible, begin waits; if freeze won first, abort
             * this batch before touching either PCM or metadata. */
            if (atomic_load_bool(&s_scratch_capture_freeze[ctx->deck]) ||
                atomic_load_bool(&s_scratch_playing[ctx->deck])) {
                atomic_store_bool(&s_scratch_capture_writing[ctx->deck], false);
                capture_scratch = false;
            }
        }
        for (int i = 0; i < samples && runtime->run; i++) {
            while (deck_pcm_free(ctx->deck, eng->sample_rate) == 0u && runtime->run) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            (void)deck_pcm_push(ctx->deck, decode_pcm[i * 2], decode_pcm[i * 2 + 1]);
            if (capture_scratch && !timeline_active(ctx->deck)) {
                /* Capture the same source frame into the scratch window (passive). */
                audio_scratch_buffer_push(scratch, decode_pcm[i * 2], decode_pcm[i * 2 + 1]);
            }
        }
        if (timeline_active(ctx->deck) && eng->timeline_preroll_pending &&
            audio_pcm_timeline_write_seq(&s_pcm_timelines[ctx->deck]) >=
                eng->timeline_preroll_frames) {
            if (audio_pcm_timeline_set_playhead(
                    &s_pcm_timelines[ctx->deck], eng->timeline_preroll_frames)) {
                eng->timeline_preroll_pending = false;
                ESP_LOGI(TAG,
                         "cue pre-roll D%u ready: history=%u frames target=%u ms",
                         (unsigned)ctx->deck,
                         (unsigned)eng->timeline_preroll_frames,
                         (unsigned)eng->output_base_ms);
            }
        }
        if (timeline_active(ctx->deck) && scratch_newest_valid) {
            sync_scratch_view_from_timeline(ctx->deck, scratch_newest_ms);
        } else if (capture_scratch && scratch_newest_valid) {
            audio_scratch_buffer_mark_newest_ms(scratch, scratch_newest_ms);
        }
        if (capture_scratch) {
            atomic_store_bool(&s_scratch_capture_writing[ctx->deck], false);
        }
        if (!timeline_active(ctx->deck) && !scratch_full_logged &&
            audio_scratch_buffer_used(scratch) >= AE_SCRATCH_CAPACITY_FRAMES) {
            scratch_full_logged = true;
            ESP_LOGI(TAG, "scratch buffer D%u filled: %u frames @ %u Hz (newest %u ms)",
                     (unsigned)ctx->deck,
                     (unsigned)audio_scratch_buffer_used(scratch),
                     (unsigned)eng->sample_rate,
                     (unsigned)scratch_newest_ms);
        }
    }

cleanup:
    /* The preload buffer / file are owned by the loader + audio_engine_stop(). */
    runtime->decode_task = NULL;
    xSemaphoreGive(ctx_tasks_done(ctx));
    vTaskDeleteWithCaps(NULL);
}

/* Consumer: pitch-resample from the ring and write PCM to the physical outputs.
 * The codec/I2S writes block on DMA, which paces real-time playback. */
#define AE_OUT_FRAMES 256
#define AE_OUTPUT_TASK_STACK 8192
/* Keep real-time audio producer/output work off the LVGL core. */
#define AE_AUDIO_TASK_CORE 0

/* Mixer scratch source callback (vinyl mode Phase 4): renders one output-rate
 * frame for the deck named by `ctx`. Steady state reads the scratch engine; the
 * release handoff (4b) cross-fades scratch -> forward per sample. Returns true if
 * audio was produced (false -> silence). Runs on the output task; the
 * engine/buffer/ring are single-reader here. */
static bool ae_scratch_render_cb(void *ctx, audio_mixer_frame_t *out)
{
    uint8_t deck = ctx ? *(const uint8_t *)ctx : 0u;
    if (out) { out->left = 0; out->right = 0; }
    if (deck >= AUDIO_ENGINE_DECK_COUNT || !out) {
        return false;
    }

    switch (scratch_handoff_load(&s_scratch_handoff[deck])) {
    case AE_SCRATCH_HANDOFF_FADE_OUT: {
        int16_t l = 0, r = 0;
        (void)audio_scratch_render(&s_scratch_engine[deck], &s_scratch_buf[deck], &l, &r);
        scratch_head_publish(deck);
        float g = s_scratch_handoff_gain[deck];
        out->left = (int16_t)((float)l * g);
        out->right = (int16_t)((float)r * g);
        g -= AE_SCRATCH_XFADE_STEP;
        if (g <= 0.0f) {
            g = 0.0f;
            s_scratch_handoff_gain[deck] = g;
            apply_pending_pitch(deck);
            if (atomic_load_bool(&s_scratch_return_paused[deck])) {
                audio_scratch_end(&s_scratch_engine[deck]);
                atomic_store_bool(&s_scratch_return_paused[deck], false);
                scratch_handoff_store(&s_scratch_handoff[deck],
                                      AE_SCRATCH_HANDOFF_RING);
                return true;
            }
            atomic_store_bool(&s_scratch_capture_reset_ready[deck], true);
            scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_FADE_IN);
            return true;
        }
        s_scratch_handoff_gain[deck] = g;
        return true;
    }
    case AE_SCRATCH_HANDOFF_FADE_IN:
    case AE_SCRATCH_HANDOFF_RING: {
        /* Resume through the normal resampler, including source/output rate and
         * pitch. Direct raw ring pops here used to play mixed-rate decks at the
         * wrong speed and left the resampler discontinuous at handoff. */
        if (deck_pcm_used(deck) == 0u) {
            return false;
        }
        float effective_pitch = s_engines[deck].pitch_factor * (1.0f + s_jog_bend[deck]);
        if (s_engines[deck].sample_rate > 0u && s_output_sample_rate > 0u) {
            effective_pitch *= (float)s_engines[deck].sample_rate /
                               (float)s_output_sample_rate;
        }
        uint32_t consumed = 0u;
        audio_mixer_frame_t f = audio_resampler_next(resampler_for_deck(deck),
                                                      effective_pitch,
                                                      pop_deck_source,
                                                      &s_scratch_ctx_deck[deck],
                                                      &consumed);
        s_scratch_handoff_consumed[deck] += consumed;
        float g = s_scratch_handoff_gain[deck];
        out->left = (int16_t)((float)f.left * g);
        out->right = (int16_t)((float)f.right * g);
        if (scratch_handoff_load(&s_scratch_handoff[deck]) == AE_SCRATCH_HANDOFF_FADE_IN) {
            g += AE_SCRATCH_XFADE_STEP;
            if (g >= 1.0f) {
                g = 1.0f;
                s_scratch_handoff_gain[deck] = g;
                /* Full gain reached; keep popping the ring until the output task
                 * hands back to the resampler at the next block boundary. */
                scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_RING);
                return true;
            }
            s_scratch_handoff_gain[deck] = g;
        }
        return true;
    }
    default: {
        int16_t l = 0, r = 0;
        bool produced = audio_scratch_render(&s_scratch_engine[deck],
                                             &s_scratch_buf[deck], &l, &r);
        scratch_head_publish(deck);
        out->left = l;
        out->right = r;
        return produced;
    }
    }
}
/* The per-deck effects run post-resampler on the shared output stream, but
 * they are initialised before the codec rate is known (EQ/filter at 44.1 kHz,
 * echo at the 48 kHz fallback). Retune them to the real output rate so
 * beat-synced echo delays and filter cutoffs land where they should. */
static void audio_output_apply_fx_sample_rate(uint32_t sample_rate)
{
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        audio_eq_set_sample_rate(&s_deck_eq[deck], sample_rate);
        s_deck_filter[deck].sample_rate_hz = sample_rate;
        s_beat_fx_filter[deck].sample_rate_hz = sample_rate;
        s_pad_fx[deck].filter.sample_rate_hz = sample_rate;
        s_beat_fx_echo[deck].sample_rate = sample_rate;
        audio_delay_fx_configure(&s_beat_fx_echo[deck], &s_beat_fx_echo[deck].config);
        s_pad_fx[deck].echo.sample_rate = sample_rate;
        audio_delay_fx_configure(&s_pad_fx[deck].echo, &s_pad_fx[deck].echo.config);
        s_beat_fx_flanger[deck].sample_rate = sample_rate;
        audio_flanger_fx_configure(&s_beat_fx_flanger[deck], &s_beat_fx_flanger[deck].config);
    }
}

static esp_err_t audio_output_service_open_codec(uint32_t sample_rate)
{
    if (sample_rate == 0) return ESP_ERR_INVALID_ARG;

    AE_LOCK();
    if (s_output_codec_open) {
        AE_UNLOCK();
        return ESP_OK;
    }

#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (s_main_i2s_tx) {
        /* PCM5102A starts at the BSP default clock; align it to the loaded
         * track before the first blocking I2S write or playback will be paced
         * at the wrong sample rate. */
        esp_err_t main_rc = bsp_audio_main_i2s_set_sample_rate(sample_rate);
        if (main_rc != ESP_OK) {
            AE_UNLOCK();
            return main_rc;
        }
        ESP_LOGI(TAG, "PCM5102A main out open @ %u Hz", (unsigned)sample_rate);
    }
#endif

    if (s_codec) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 2,
            .sample_rate     = sample_rate,
        };
        if (esp_codec_dev_open(s_codec, &fs) != 0) {
            AE_UNLOCK();
            return ESP_FAIL;
        }
    }
    s_output_codec_open = true;
    s_output_sample_rate = sample_rate;
    audio_output_apply_fx_sample_rate(sample_rate);
    (void)monitor_pcm_link_set_format(sample_rate, 2u, 16u);
#if CONFIG_MONITOR_PCM_LINK_ENABLED && !CONFIG_MONITOR_PCM_LINK_BENCH_TONE
    /* Product path: start publishing real hp_out to the S3 monitor link now
       that the output rate is known. The bench-tone build enables the link
       from its own generator task instead. */
    monitor_pcm_link_set_enabled(true);
#endif
    ESP_LOGI(TAG, "shared codec open @ %u Hz", (unsigned)sample_rate);
    AE_UNLOCK();
    return ESP_OK;
}

static audio_output_headphone_mode_t output_headphone_mode(void)
{
    if (s_headphone_mode == AUDIO_HEADPHONE_MODE_MASTER_MONO) {
        return AUDIO_OUTPUT_HEADPHONE_MASTER_MONO;
    }
    if (s_headphone_mode == AUDIO_HEADPHONE_MODE_CUE_MONO) {
        return AUDIO_OUTPUT_HEADPHONE_CUE_MONO;
    }
    return AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO;
}

static esp_err_t audio_output_write_main(const int16_t *frames, size_t bytes)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_main_i2s_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t written = 0;
    esp_err_t rc = i2s_channel_write(s_main_i2s_tx, frames, bytes, &written, portMAX_DELAY);
    if (rc != ESP_OK) {
        return rc;
    }
    return written == bytes ? ESP_OK : ESP_FAIL;
#else
    (void)frames;
    (void)bytes;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void ae_output_task(void *arg)
{
    (void)arg;
    int16_t master_out[AE_OUT_FRAMES * 2];
    int16_t hp_out[AE_OUT_FRAMES * 2];
    while (s_output_run) {
        if (!s_output_codec_open) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        int64_t block_start_us = esp_timer_get_time();

        /* Scratch handoff (4b): once the resumed forward audio has faded up to
         * full gain, hand the deck back to the resampler + ring. Done here, before
         * the deck structs capture s_scratch_playing, so this block already routes
         * through the resampler. A deck that stops (EOF/pause/stop) mid-scratch or
         * mid-handoff would otherwise be skipped by the mixer, so its render
         * callback never runs and s_scratch_playing sticks true (silent deck +
         * frozen capture); tear the scratch state down here in that case. */
        for (uint8_t d = 0; d < AUDIO_ENGINE_DECK_COUNT; d++) {
            if (__atomic_exchange_n(&s_scratch_abort_seek_requested[d], false,
                                    __ATOMIC_ACQ_REL)) {
                uint32_t target = __atomic_load_n(&s_scratch_abort_seek_target_ms[d],
                                                  __ATOMIC_ACQUIRE);
                /* External transport wins over scratch. Teardown runs here, on
                 * the sole scratch-render owner, then the deck stays muted until
                 * decode has flushed/refilled at the requested generation. */
                audio_scratch_end(&s_scratch_engine[d]);
                s_scratch_handoff_gain[d] = 1.0f;
                scratch_handoff_store(&s_scratch_handoff[d], AE_SCRATCH_HANDOFF_NONE);
                atomic_store_bool(&s_scratch_playing[d], false);
                atomic_store_bool(&s_scratch_capture_freeze[d], false);
                atomic_store_bool(&s_scratch_capture_reset_ready[d], false);
                atomic_store_bool(&s_scratch_abort_seek_waiting[d], true);
                apply_pending_pitch(d);
                if (audio_engine_seek_for_deck_reason(d, target,
                                                      AE_SEEK_REASON_SCRATCH_ABORT) != ESP_OK) {
                    atomic_store_bool(&s_scratch_abort_seek_waiting[d], false);
                }
                atomic_store_bool(&s_scratch_regrab_requested[d], false);
                continue; /* external transport has priority over a re-grab */
            }
            if (__atomic_exchange_n(&s_scratch_regrab_requested[d], false,
                                    __ATOMIC_ACQ_REL)) {
                s_scratch_handoff_gain[d] = 1.0f;
                scratch_handoff_store(&s_scratch_handoff[d], AE_SCRATCH_HANDOFF_NONE);
                continue;
            }
            if (scratch_handoff_load(&s_scratch_handoff[d]) == AE_SCRATCH_HANDOFF_RING) {
                s_scratch_handoff_gain[d] = 1.0f;
                scratch_handoff_store(&s_scratch_handoff[d], AE_SCRATCH_HANDOFF_NONE);
                atomic_store_bool(&s_scratch_playing[d], false);
                atomic_store_bool(&s_scratch_capture_freeze[d], false);
            } else if (atomic_load_bool(&s_scratch_playing[d]) && !deck_output_active(d)) {
                audio_scratch_end(&s_scratch_engine[d]);
                s_scratch_handoff_gain[d] = 1.0f;
                scratch_handoff_store(&s_scratch_handoff[d], AE_SCRATCH_HANDOFF_NONE);
                atomic_store_bool(&s_scratch_playing[d], false);
                atomic_store_bool(&s_scratch_capture_freeze[d], false);
            }
        }

        float deck0_gain = 1.0f;
        float deck1_gain = 1.0f;
        audio_engine_get_output_gains(&deck0_gain, &deck1_gain);
        /* Pre-fader gain (pregain x master trim) for the VU meters, so they track
         * the TRIM knob independent of the channel fader/crossfader. */
        float deck0_prefader_gain =
            pregain_gain_from_raw(atomic_load_u16(&s_pregain[0])) * s_master_trim;
        float deck1_prefader_gain =
            pregain_gain_from_raw(atomic_load_u16(&s_pregain[1])) * s_master_trim;
        bool smart_cfx_enabled = atomic_load_bool(&s_smart_cfx_enabled);
        bool pfl0_enabled = atomic_load_bool(&s_pfl_enabled[AUDIO_ENGINE_COMPAT_DECK]);
        bool pfl1_enabled = atomic_load_bool(&s_pfl_enabled[1u]);
        bool master_cue_enabled = atomic_load_bool(&s_master_cue_enabled);
        uint16_t headphone_mix = atomic_load_u16(&s_headphone_mix);
        uint16_t headphone_level = atomic_load_u16(&s_headphone_level);

        const uint8_t deck0_index = AUDIO_ENGINE_COMPAT_DECK;
        const uint8_t deck1_index = 1u;
        audio_output_mixer_deck_t deck0 = {
            .active = deck_output_active(deck0_index),
            .pitch_factor = s_engines[deck0_index].pitch_factor * (1.0f + s_jog_bend[deck0_index]),
            .source_sample_rate = s_engines[deck0_index].sample_rate,
            .output_sample_rate = s_output_sample_rate,
            .gain = deck0_gain,
            .eq = &s_deck_eq[deck0_index],
            .filter = &s_deck_filter[deck0_index],
            .filter_enabled = smart_cfx_enabled,
            .beat_fx_filter = &s_beat_fx_filter[deck0_index],
            .beat_fx_filter_enabled = atomic_load_bool(&s_beat_fx_filter_enabled[deck0_index]),
            .beat_fx_flanger = &s_beat_fx_flanger[deck0_index],
            .beat_fx_flanger_enabled = atomic_load_bool(&s_beat_fx_flanger_enabled[deck0_index]),
            .beat_fx_echo = &s_beat_fx_echo[deck0_index],
            .beat_fx_echo_enabled = atomic_load_bool(&s_beat_fx_echo_enabled[deck0_index]),
            .pad_fx = &s_pad_fx[deck0_index],
            .resampler = resampler_for_deck(deck0_index),
            .pop_source = pop_deck_source,
            .source_ctx = &s_scratch_ctx_deck[deck0_index],
            .scratch_active = atomic_load_bool(&s_scratch_playing[deck0_index]),
            .scratch_render = ae_scratch_render_cb,
            .scratch_ctx = &s_scratch_ctx_deck[deck0_index],
        };
        audio_output_mixer_deck_t deck1 = {
            .active = deck_output_active(deck1_index),
            .pitch_factor = s_engines[deck1_index].pitch_factor * (1.0f + s_jog_bend[deck1_index]),
            .source_sample_rate = s_engines[deck1_index].sample_rate,
            .output_sample_rate = s_output_sample_rate,
            .gain = deck1_gain,
            .eq = &s_deck_eq[deck1_index],
            .filter = &s_deck_filter[deck1_index],
            .filter_enabled = smart_cfx_enabled,
            .beat_fx_filter = &s_beat_fx_filter[deck1_index],
            .beat_fx_filter_enabled = atomic_load_bool(&s_beat_fx_filter_enabled[deck1_index]),
            .beat_fx_flanger = &s_beat_fx_flanger[deck1_index],
            .beat_fx_flanger_enabled = atomic_load_bool(&s_beat_fx_flanger_enabled[deck1_index]),
            .beat_fx_echo = &s_beat_fx_echo[deck1_index],
            .beat_fx_echo_enabled = atomic_load_bool(&s_beat_fx_echo_enabled[deck1_index]),
            .pad_fx = &s_pad_fx[deck1_index],
            .resampler = resampler_for_deck(deck1_index),
            .pop_source = pop_deck_source,
            .source_ctx = &s_scratch_ctx_deck[deck1_index],
            .scratch_active = atomic_load_bool(&s_scratch_playing[deck1_index]),
            .scratch_render = ae_scratch_render_cb,
            .scratch_ctx = &s_scratch_ctx_deck[deck1_index],
        };

        /* Decay the jog nudge once per output block; snap tiny residuals to 0. */
        for (uint8_t d = 0; d < AUDIO_ENGINE_DECK_COUNT; d++) {
            s_jog_bend[d] *= AE_JOG_BEND_DECAY;
            if (s_jog_bend[d] < 0.0005f && s_jog_bend[d] > -0.0005f) {
                s_jog_bend[d] = 0.0f;
            }
        }

        if (!deck0.active && !deck1.active) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t consumed[AUDIO_ENGINE_DECK_COUNT] = { 0 };
        s_scratch_handoff_consumed[0] = 0u;
        s_scratch_handoff_consumed[1] = 0u;
        uint16_t block_peak[AUDIO_ENGINE_DECK_COUNT] = { 0 };
        audio_mixer_limiter_stats_t block_limiter_stats = { 0 };

        for (int i = 0; i < AE_OUT_FRAMES; i++) {
            uint32_t frame_consumed0 = 0;
            uint32_t frame_consumed1 = 0;

            audio_output_mix_result_t mix = audio_output_mixer_next_full_with_headphone_level(
                &deck0,
                &deck1,
                pfl0_enabled,
                pfl1_enabled,
                output_headphone_mode(),
                headphone_mix,
                headphone_level,
                master_cue_enabled,
                &frame_consumed0,
                &frame_consumed1,
                &block_limiter_stats);

            uint16_t peak0 = frame_peak_prefader(mix.deck_frame[0], deck0_prefader_gain);
            uint16_t peak1 = frame_peak_prefader(mix.deck_frame[1], deck1_prefader_gain);
            if (peak0 > block_peak[deck0_index]) block_peak[deck0_index] = peak0;
            if (peak1 > block_peak[deck1_index]) block_peak[deck1_index] = peak1;

            consumed[deck0_index] += frame_consumed0;
            consumed[deck1_index] += frame_consumed1;

            master_out[i * 2] = mix.master.left;
            master_out[i * 2 + 1] = mix.master.right;
            hp_out[i * 2] = mix.headphone.left;
            hp_out[i * 2 + 1] = mix.headphone.right;
        }
        consumed[deck0_index] += s_scratch_handoff_consumed[deck0_index];
        consumed[deck1_index] += s_scratch_handoff_consumed[deck1_index];
        (void)monitor_pcm_link_write_nonblocking(hp_out, AE_OUT_FRAMES);
        esp_err_t main_rc = audio_output_write_main(master_out, AE_OUT_FRAMES * 2 * sizeof(int16_t));
        /* When ES8311 is disabled the loop paces on the PCM5102A blocking
           write above; hp_out still reaches the FLX4 phones over the link. */
        esp_err_t hp_rc = ESP_ERR_NOT_SUPPORTED;
        if (s_codec) {
            hp_rc = esp_codec_dev_write(s_codec, hp_out, (int)(AE_OUT_FRAMES * 2 * sizeof(int16_t)));
        }

        if (hp_rc == ESP_OK || main_rc == ESP_OK || main_rc == ESP_ERR_NOT_SUPPORTED) {
            AE_LOCK();
            update_deck_output_position(deck0_index, consumed[deck0_index]);
            update_deck_output_position(deck1_index, consumed[deck1_index]);
            record_deck_peak_value(deck0_index, block_peak[deck0_index]);
            record_deck_peak_value(deck1_index, block_peak[deck1_index]);
            record_deck_ui_peak(deck0_index, block_peak[deck0_index]);
            record_deck_ui_peak(deck1_index, block_peak[deck1_index]);
            s_limiter_stats.limited_samples += block_limiter_stats.limited_samples;
            s_limiter_stats.positive_overloads += block_limiter_stats.positive_overloads;
            s_limiter_stats.negative_overloads += block_limiter_stats.negative_overloads;
            if (block_limiter_stats.peak_input_abs > s_limiter_stats.peak_input_abs) {
                s_limiter_stats.peak_input_abs = block_limiter_stats.peak_input_abs;
            }
            AE_UNLOCK();
        }
        int64_t block_elapsed_us = esp_timer_get_time() - block_start_us;
        uint32_t block_period_us = audio_output_block_period_us(s_output_sample_rate);
        uint32_t late_warning_us = audio_output_late_warning_threshold_us(s_output_sample_rate);
        ae_diag_record_output_block(block_elapsed_us > 0 ? (uint32_t)block_elapsed_us : 0u,
                                    late_warning_us > 0u ? late_warning_us : block_period_us,
                                    consumed[deck0_index],
                                    consumed[deck1_index],
                                    deck0.active,
                                    deck1.active,
                                    &s_limiter_stats);
        uint32_t block_delay_ms = audio_output_remaining_delay_ms(
            s_output_sample_rate,
            block_elapsed_us > 0 ? (uint32_t)block_elapsed_us : 0u);
        if (block_delay_ms > 0u) {
            vTaskDelay(pdMS_TO_TICKS(block_delay_ms));
        } else {
            taskYIELD();
        }
    }
    if (s_output_codec_open) {
        if (s_codec) esp_codec_dev_close(s_codec);
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
    if (xTaskCreatePinnedToCore(ae_output_task, "ae_output", AE_OUTPUT_TASK_STACK, NULL, 6,
                                &s_output_task, AE_AUDIO_TASK_CORE) != pdPASS) {
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
            if (s_codec) esp_codec_dev_close(s_codec);
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

/* Tear down all vinyl-scratch playback state for a deck: cancel the read head,
 * disarm any release-handoff, and route the deck back to the resampler. Called
 * on (re)load/stop/error reset so a deck that was scratching (or mid-handoff)
 * when the track changed can never be left routed to the scratch source with a
 * frozen capture buffer — which would leave the freshly loaded deck silent. */
static void clear_scratch_playback_state(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return;
    audio_scratch_end(&s_scratch_engine[deck]);
    s_scratch_handoff_gain[deck] = 1.0f;
    scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_NONE);
    atomic_store_bool(&s_scratch_playing[deck], false);
    atomic_store_bool(&s_scratch_capture_reset_ready[deck], false);
    atomic_store_bool(&s_scratch_capture_freeze[deck], false);
    atomic_store_bool(&s_scratch_capture_writing[deck], false);
    atomic_store_bool(&s_scratch_abort_seek_requested[deck], false);
    atomic_store_bool(&s_scratch_abort_seek_waiting[deck], false);
    atomic_store_bool(&s_scratch_regrab_requested[deck], false);
    atomic_store_bool(&s_scratch_started_paused[deck], false);
    atomic_store_bool(&s_scratch_return_paused[deck], false);
    atomic_store_bool(&s_pending_pitch_valid[deck], false);
}

static void audio_engine_reset_state(audio_engine_state_t *eng, esp_err_t err, const char *err_text)
{
    /* Clear any lingering platter-hold + scratch playback so a freshly (re)loaded
     * deck is never stuck silenced. eng-indexed via pointer arithmetic. */
    if (eng >= s_engines && eng < s_engines + AUDIO_ENGINE_DECK_COUNT) {
        uint8_t deck = (uint8_t)(eng - s_engines);
        atomic_store_bool(&s_deck_hold[deck], false);
        clear_scratch_playback_state(deck);
    }
    memset(eng, 0, sizeof(*eng));
    eng->pitch_factor = 1.0f;
    eng->load_progress = 100;
    eng->last_error = err;
    snprintf(eng->last_error_text, sizeof(eng->last_error_text), "%s", err_text ? err_text : "OK");
    mp3dec_init(&eng->dec);
}

static esp_err_t audio_engine_stop_for_deck(uint8_t deck);

/* ── audio_engine_init ────────────────────────────────────────────────────── */
esp_err_t audio_engine_init(void)
{
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        audio_engine_reset_state(&s_engines[i], ESP_OK, "OK");
    }
    reset_all_pcm_rings();
#if AE_FW
    reset_all_resamplers();
    reset_all_fw_preloads();
    reset_all_fw_runtimes();
    reset_all_fw_task_contexts();
    ae_diag_reset();
#endif
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        atomic_store_u16(&s_channel_volume[i], AUDIO_MIXER_CONTROL_MAX);
        s_pcm_underrun_count[i] = 0u;
        atomic_store_u16(&s_pregain[i], AUDIO_MIXER_CONTROL_CENTER);
        atomic_store_bool(&s_pfl_enabled[i], false);
        s_deck_peak[i] = 0;
        s_deck_ui_peak[i] = 0;
        audio_eq_init(&s_deck_eq[i], 44100u);
        audio_filter_init(&s_deck_filter[i], 44100u);
        atomic_store_u16(&s_deck_filter_raw[i], AUDIO_FILTER_RAW_CENTER);
        atomic_store_u16(&s_deck_filter_effective[i], AUDIO_FILTER_RAW_CENTER);
        audio_filter_init(&s_beat_fx_filter[i], 44100u);
        atomic_store_bool(&s_beat_fx_filter_enabled[i], false);
    }
    init_beat_fx_echo_buffers();
    init_beat_fx_flanger_buffers();
    init_pad_fx_buffers();
    init_scratch_buffers();
    atomic_store_u16(&s_crossfader, AUDIO_MIXER_CONTROL_CENTER);
    s_master_trim = 1.0f;
    atomic_store_u16(&s_master_volume, AUDIO_MIXER_CONTROL_MAX);
    atomic_store_u16(&s_headphone_mix, AUDIO_MIXER_CONTROL_MAX);
    atomic_store_u16(&s_headphone_level, AUDIO_MIXER_CONTROL_MAX);
    atomic_store_bool(&s_master_cue_enabled, true);
    s_cue_mode = 0;
    s_headphone_mode = AUDIO_HEADPHONE_MODE_MASTER_MONO;
    s_limiter_stats = (audio_mixer_limiter_stats_t){ 0 };
    atomic_store_bool(&s_smart_cfx_enabled, false);
    atomic_store_bool(&s_smart_fader_enabled, false);
    esp_err_t monitor_rc = monitor_pcm_link_init();
    if (monitor_rc != ESP_OK) {
        ESP_LOGE(TAG, "monitor_pcm_link_init failed: %d", (int)monitor_rc);
        return monitor_rc;
    }

#if AE_FW
    /* Firmware: the ES8311 codec was created by bsp_audio_init(); grab the handle.
     * The I2S clock is configured per-track in audio_engine_load via codec_open. */
    s_codec = bsp_audio_get_codec_dev();
    s_main_i2s_tx = bsp_audio_get_main_i2s_tx();
    if (!s_codec && !s_main_i2s_tx) {
        ESP_LOGE(TAG, "audio_engine_init: no audio output ready (call bsp_audio_init first)");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_file_mutex) s_file_mutex = xSemaphoreCreateRecursiveMutex();
    bool tasks_done_ok = true;
    for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
        if (!s_tasks_done[i]) {
            /* Each deck runs at most a loader + decoder + output task. */
            s_tasks_done[i] = xSemaphoreCreateCounting(3, 0);
        }
        if (!s_tasks_done[i]) tasks_done_ok = false;
    }
    if (!s_output_done) {
        s_output_done = xSemaphoreCreateCounting(1, 0);
    }
    if (!s_file_mutex || !tasks_done_ok || !s_output_done) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "audio_engine_init: output ready (ES8311=%s, PCM5102A=%s)",
             s_codec ? "on" : "off", s_main_i2s_tx ? "on" : "off");
#endif

    return ESP_OK;
}

static esp_err_t audio_engine_load_for_deck(uint8_t deck,
                                            const char *mp3_path,
                                            const uint32_t *pvbr_400,
                                            uint32_t duration_ms)
{
    audio_engine_state_t *eng = &s_engines[deck];
#if AE_FW
    audio_pcm_ring_t *ring = &s_pcm_rings[deck]; /* legacy fallback in task context */
#endif
    eng->last_error = ESP_OK;
    snprintf(eng->last_error_text, sizeof(eng->last_error_text), "OK");

    if (!mp3_path) {
        eng->last_error = ESP_ERR_INVALID_ARG;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "INVALID ARG");
        return ESP_ERR_INVALID_ARG;
    }

    if (eng->loaded) {
        esp_err_t stop_rc = audio_engine_stop_for_deck(deck);
        if (stop_rc != ESP_OK) {
            eng->last_error = stop_rc;
            snprintf(eng->last_error_text, sizeof(eng->last_error_text), "STOP ERR");
            return stop_rc;
        }
    }

    eng->loading = true;   /* cleared when the codec opens (FW) / at end (PC) */
    eng->load_progress = 0;

#if AE_FW
    audio_format_t detected_format = audio_format_detect_path(mp3_path);
    /* MP3, WAV and FLAC all preload into PSRAM and decode from the buffer;
     * unknown extensions fall back to MP3 (minimp3 resyncs on the first frame). */
    eng->format = (detected_format == AUDIO_FORMAT_UNKNOWN) ? AUDIO_FORMAT_MP3 : detected_format;
    audio_fw_preload_t *fw = &s_fw_preloads[deck];
    audio_fw_preload_set_path(fw, mp3_path);
    eng->fp = NULL;
#else
    audio_format_t detected_format = audio_format_detect_path(mp3_path);
    FILE *fp = fopen(mp3_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot open: %s", mp3_path);
        eng->last_error = ESP_ERR_NOT_FOUND;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "NOT FOUND");
        eng->loading = false;
        eng->load_progress = 100;
        return ESP_ERR_NOT_FOUND;
    }
    eng->fp = fp;
    fseek(fp, 0, SEEK_END);
    long pc_file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    eng->file_size = pc_file_size > 0 ? (size_t)pc_file_size : 0u;
    eng->format = detected_format;
    if (detected_format == AUDIO_FORMAT_WAV || detected_format == AUDIO_FORMAT_FLAC) {
        /* WAV and FLAC both decode through the audio_decoder abstraction on the
         * PC/simulator build (MP3 stays on minimp3). */
        fclose(fp);
        eng->fp = NULL;
        esp_err_t dec_rc = audio_decoder_open(&eng->decoder, mp3_path);
        if (dec_rc != ESP_OK) {
            eng->last_error = dec_rc;
            snprintf(eng->last_error_text, sizeof(eng->last_error_text), "DECODER ERR");
            eng->loading = false;
            eng->load_progress = 100;
            return dec_rc;
        }
        eng->decoder_open = true;
        eng->sample_rate = eng->decoder.info.sample_rate;
        eng->channels = eng->decoder.info.channels;
        if (duration_ms == 0u && eng->sample_rate > 0u) {
            duration_ms = (uint32_t)((eng->decoder.info.total_frames * 1000ull) /
                                     (uint64_t)eng->sample_rate);
        }
    } else {
        eng->fp = fp;
    }
#endif

    eng->duration_ms = duration_ms;
    if (eng->format == AUDIO_FORMAT_MP3 || eng->format == AUDIO_FORMAT_UNKNOWN) {
        eng->sample_rate = 0u;   /* latched on first decoded frame */
        eng->channels    = 2;
    }

    if (pvbr_400) {
        bool any_nonzero = false;
        for (uint32_t i = 1u; i < AUDIO_PVBR_LEN; i++) {
            if (pvbr_400[i] > 0) {
                any_nonzero = true;
                break;
            }
        }
        if (any_nonzero) {
            memcpy(eng->pvbr, pvbr_400, AUDIO_PVBR_LEN * sizeof(uint32_t));
            eng->has_pvbr = true;
            ESP_LOGI(TAG, "PVBR seek table loaded and verified (has non-zero values)");
        } else {
            ESP_LOGI(TAG, "PVBR table contains only zeros; using linear seek fallback");
            memset(eng->pvbr, 0, sizeof eng->pvbr);
            eng->has_pvbr = false;
        }
    } else {
        memset(eng->pvbr, 0, sizeof eng->pvbr);
        eng->has_pvbr = false;
    }

    eng->seek_base_ms      = 0u;
    eng->frames_since_seek = 0u;
    eng->output_base_ms    = 0u;
    eng->output_frames_since_seek = 0u;
    eng->playing           = false;
    eng->paused            = false;
    eng->eof               = false;
    eng->loaded            = true;

    mp3dec_init(&eng->dec);
    deck_pcm_reset(deck);

    ESP_LOGI(TAG, "track load D%u: %s dur=%u ms pvbr=%s",
             (unsigned)deck, mp3_path, (unsigned)duration_ms, pvbr_400 ? "yes" : "no");

#if AE_FW
    audio_fw_runtime_t *runtime = &s_fw_runtimes[deck];
    audio_fw_task_context_t *task_ctx = &s_fw_task_contexts[deck];
    audio_resampler_reset(&s_resamplers[deck]);
    audio_fw_runtime_begin_load(runtime);
    audio_fw_preload_begin_load(fw);
    audio_fw_task_plan_t task_plan =
        audio_fw_task_plan_for_deck(deck,
                                    AUDIO_ENGINE_COMPAT_DECK,
                                    audio_fw_output_task_running());
    audio_fw_task_context_bind(task_ctx,
                               deck,
                               fw,
                               runtime,
                               eng,
                               ring,
                               &s_resamplers[deck],
                               task_plan);
    if (s_tasks_done[deck]) {
        while (xSemaphoreTake(s_tasks_done[deck], 0) == pdTRUE) {
            /* drain stale task-exit signals from a previous load of this deck */
        }
    }
    if (task_plan.start_loader) {
        if (xTaskCreatePinnedToCore(ae_loader_task, "ae_loader", 4096, task_ctx, 5,
                                    (TaskHandle_t *)&runtime->loader_task,
                                    AE_AUDIO_TASK_CORE) == pdPASS) {
            audio_fw_runtime_mark_task_started(runtime);
        } else {
            ESP_LOGE(TAG, "failed to create ae_loader task");
        }
    }
    if (task_plan.start_decode) {
        if (xTaskCreatePinnedToCoreWithCaps(ae_decode_task, "ae_decode", 49152, task_ctx, 5,
                                            (TaskHandle_t *)&runtime->decode_task,
                                            AE_AUDIO_TASK_CORE,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
            audio_fw_runtime_mark_task_started(runtime);
        } else {
            ESP_LOGE(TAG, "failed to create ae_decode task");
        }
    }
    if (task_plan.start_output) {
        if (xTaskCreatePinnedToCore(ae_output_task, "ae_output", AE_OUTPUT_TASK_STACK, task_ctx, 6,
                                    (TaskHandle_t *)&runtime->output_task,
                                    AE_AUDIO_TASK_CORE) == pdPASS) {
            audio_fw_runtime_mark_task_started(runtime);
        } else {
            ESP_LOGE(TAG, "failed to create ae_output task");
        }
    }
    esp_err_t output_rc = audio_output_service_ensure_started();
    if (output_rc != ESP_OK) {
        ESP_LOGE(TAG, "failed to start shared output task");
        eng->last_error = output_rc;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "OUTPUT TASK ERR");
        eng->loading = false;
        eng->load_progress = 100;
        runtime->run = false;
        int exited = 0;
        for (int i = 0; i < runtime->tasks_started; i++) {
            if (xSemaphoreTake(s_tasks_done[deck], pdMS_TO_TICKS(1500)) == pdTRUE) {
                exited++;
            }
        }
        /* Only reclaim the PSRAM buffer once every task that could still be
         * reading it (the loader's fread target) has actually exited; freeing
         * it under a stuck loader would be a use-after-free. */
        if (exited == runtime->tasks_started && fw->buf) {
            heap_caps_free(fw->buf);
            fw->buf = NULL;
        } else if (exited != runtime->tasks_started) {
            ESP_LOGE(TAG, "load abort: %d/%d tasks exited; leaking preload buffer",
                     exited, runtime->tasks_started);
        }
        audio_engine_reset_state(eng, output_rc, "OUTPUT TASK ERR");
        audio_fw_runtime_mark_stopped(runtime);
        audio_fw_task_context_reset(task_ctx);
        return output_rc;
    }
    if (runtime->tasks_started != task_plan.expected_tasks) {
        eng->last_error = ESP_ERR_NO_MEM;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "TASK CREATE ERR");
        eng->loading = false;
        eng->load_progress = 100;
        runtime->run = false;
        int exited = 0;
        for (int i = 0; i < runtime->tasks_started; i++) {
            if (xSemaphoreTake(s_tasks_done[deck], pdMS_TO_TICKS(1500)) == pdTRUE) {
                exited++;
            }
        }
        if (runtime->codec_open) {
            if (s_codec) esp_codec_dev_close(s_codec);
            runtime->codec_open = false;
        }
        /* Same rule as the OUTPUT TASK ERR path: never free the buffer while a
         * task that reads it might still be alive. */
        if (exited == runtime->tasks_started && fw->buf) {
            heap_caps_free(fw->buf);
            fw->buf = NULL;
        } else if (exited != runtime->tasks_started) {
            ESP_LOGE(TAG, "load abort: %d/%d tasks exited; leaking preload buffer",
                     exited, runtime->tasks_started);
        }
        audio_engine_reset_state(eng, ESP_ERR_NO_MEM, "TASK CREATE ERR");
        audio_fw_runtime_mark_stopped(runtime);
        audio_fw_task_context_reset(task_ctx);
        return ESP_ERR_NO_MEM;
    }
#endif

#if !AE_FW
    eng->loading = false;
    eng->load_progress = 100;
#endif

    return ESP_OK;
}

/* ── audio_engine_load ────────────────────────────────────────────────────── */
esp_err_t audio_engine_load(const char     *mp3_path,
                             const uint32_t *pvbr_400,
                             uint32_t        duration_ms)
{
    return audio_engine_load_for_deck(AUDIO_ENGINE_COMPAT_DECK, mp3_path, pvbr_400, duration_ms);
}

static esp_err_t audio_engine_play_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded) return ESP_ERR_INVALID_STATE;

    eng->paused  = false;
    eng->playing = true;
    eng->eof     = false; /* allow replay if previously at end */
    return ESP_OK;
}

/* ── audio_engine_play ────────────────────────────────────────────────────── */
esp_err_t audio_engine_play(void)
{
    return audio_engine_play_for_deck(AUDIO_ENGINE_COMPAT_DECK);
}

static esp_err_t audio_engine_pause_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded) return ESP_ERR_INVALID_STATE;

    eng->playing = false;
    eng->paused  = true;
    return ESP_OK;
}

/* ── audio_engine_pause ───────────────────────────────────────────────────── */
esp_err_t audio_engine_pause(void)
{
    return audio_engine_pause_for_deck(AUDIO_ENGINE_COMPAT_DECK);
}

/* ── audio_engine_stop ────────────────────────────────────────────────────── */
static esp_err_t audio_engine_stop_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded) return ESP_OK;

    eng->playing = false;
    eng->paused  = false;
    eng->loading = false;
    eng->load_progress = 100;

#if AE_FW
    audio_fw_runtime_t *runtime = &s_fw_runtimes[deck];
    if (runtime->run || runtime->tasks_started > 0) {
        runtime->run = false;
        eng->eof = false;   /* wake decode task if parked at EOF */
        if (s_tasks_done[deck]) {
            int exited = 0;
            for (int i = 0; i < runtime->tasks_started; i++) {
                if (xSemaphoreTake(s_tasks_done[deck], pdMS_TO_TICKS(1500)) == pdTRUE) {
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
        audio_fw_task_context_reset(&s_fw_task_contexts[deck]);
    }
#endif

    AE_LOCK();
    if (eng->fp) { fclose(eng->fp); eng->fp = NULL; }
    if (eng->decoder_open) {
        audio_decoder_close(&eng->decoder);
        eng->decoder_open = false;
    }
#if AE_FW
    if (eng->flac) {
        drflac_close((drflac *)eng->flac);
        eng->flac = NULL;
    }
    eng->flac_ready = false;
#endif
    eng->file_buf  = NULL;
    eng->file_size = 0;
    eng->file_pos  = 0;
    if (eng->seek_table) {
#if AE_FW
        heap_caps_free(eng->seek_table);
#else
        free(eng->seek_table);
#endif
        eng->seek_table = NULL;
    }
    eng->seek_table_len = 0;
    AE_UNLOCK();

#if AE_FW
    audio_fw_preload_t *fw = &s_fw_preloads[deck];
    if (fw->buf) { heap_caps_free(fw->buf); fw->buf = NULL; }
    audio_fw_preload_begin_load(fw);
#endif

    audio_engine_reset_state(eng, ESP_OK, "OK");
    deck_pcm_reset(deck);
    audio_scratch_buffer_reset(&s_scratch_buf[deck]);

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

esp_err_t audio_engine_stop(void)
{
    return audio_engine_stop_for_deck(AUDIO_ENGINE_COMPAT_DECK);
}

/* ── audio_engine_seek ────────────────────────────────────────────────────── */
static esp_err_t audio_engine_seek_for_deck_reason(uint8_t deck,
                                                   uint32_t position_ms,
                                                   ae_seek_reason_t reason)
{
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded || (!eng->fp && !eng->file_buf && !eng->decoder_open)) return ESP_ERR_INVALID_STATE;

    AE_LOCK();
    /* The decode task writes the same seek fields under the lock (loop-wrap
     * seek and end-of-handling clear), so a user seek must publish them under
     * the lock too — otherwise it can be lost or downgraded to a no-flush
     * loop seek when the writes interleave. */
    eng->seek_target_ms = position_ms;
    eng->seek_reason    = (uint8_t)reason;
    eng->seek_requested = true;
    eng->eof            = false;  /* also wakes decode thread if at EOF */
    eng->output_base_ms = position_ms;
    eng->output_frames_since_seek = 0u;
    eng->seek_base_ms = position_ms;
    eng->frames_since_seek = 0u;
#if !AE_FW
    /* PC/simulator has no decode task to service seek_requested, so flush the
     * ring here. On firmware the decode task owns the ring/resampler flush (it
     * runs on the same core as the output-task consumer, so the reset never
     * races a concurrent pop from another core). */
    deck_pcm_reset(deck);
#endif

    if (eng->decoder_open && eng->sample_rate > 0u) {
        uint64_t frame = ((uint64_t)position_ms * (uint64_t)eng->sample_rate) / 1000ull;
        (void)audio_decoder_seek_frame(&eng->decoder, frame);
    }

    AE_UNLOCK();

    return ESP_OK;
}

static esp_err_t audio_engine_request_user_seek(uint8_t deck, uint32_t position_ms)
{
#if AE_FW
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        __atomic_store_n(&s_scratch_abort_seek_target_ms[deck], position_ms,
                         __ATOMIC_RELEASE);
        atomic_store_bool(&s_scratch_abort_seek_requested[deck], true);
        return ESP_OK;
    }
#endif
    return audio_engine_seek_for_deck_reason(deck, position_ms, AE_SEEK_REASON_USER);
}

esp_err_t audio_engine_seek(uint32_t position_ms)
{
    return audio_engine_request_user_seek(AUDIO_ENGINE_COMPAT_DECK, position_ms);
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
    audio_engine_deck_set_pitch(AUDIO_ENGINE_COMPAT_DECK, raw_pitch);
}

static void audio_engine_set_pitch_percent_for_deck(uint8_t deck, float percent)
{
    float factor = 1.0f + (percent / 100.0f);
    /* Clamp to ±20% to stay sane even if fader value is out of range */
    if (factor < 0.80f) factor = 0.80f;
    if (factor > 1.20f) factor = 1.20f;
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        s_pending_pitch_factor[deck] = factor;
        atomic_store_bool(&s_pending_pitch_valid[deck], true);
        return;
    }
    s_engines[deck].pitch_factor = factor;
}

static void audio_engine_set_pitch_for_deck(uint8_t deck, int16_t raw_pitch)
{
    audio_engine_set_pitch_percent_for_deck(deck, audio_engine_raw_pitch_to_percent(raw_pitch));
}

float audio_engine_raw_pitch_to_percent(int16_t raw_pitch)
{
    return ((8192.0f - (float)raw_pitch) / 8192.0f) * 10.0f;
}

void audio_engine_set_pitch_percent(float percent)
{
    audio_engine_deck_set_pitch_percent(AUDIO_ENGINE_COMPAT_DECK, percent);
}

/* ── audio_engine_position_ms ─────────────────────────────────────────────── */
uint32_t audio_engine_position_ms(void)
{
    return audio_engine_deck_position_ms(AUDIO_ENGINE_COMPAT_DECK);
}

static uint32_t audio_engine_position_ms_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    /* While the scratch source is audible, its fractional head is the playback
     * authority. Reporting the normal cursor made the waveform extrapolate at
     * +1x and snap back on every UI sample. FADE_IN already follows the normal
     * timeline playhead selected on release. */
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        ae_scratch_handoff_t phase =
            (ae_scratch_handoff_t)scratch_handoff_load(&s_scratch_handoff[deck]);
        audio_scratch_buffer_t *b = &s_scratch_buf[deck];
        if ((phase == AE_SCRATCH_HANDOFF_NONE ||
             phase == AE_SCRATCH_HANDOFF_FADE_OUT) &&
            b->newest_valid && b->sample_rate > 0u) {
            float head_back = scratch_head_snapshot(deck);
            return audio_scratch_track_position_ms(
                b->newest_pos_ms, head_back, b->sample_rate,
                eng->loop_active, eng->loop_start_ms, eng->loop_end_ms);
        }
    }
    AE_LOCK();
    if (!eng->loaded || eng->sample_rate == 0) {
        uint32_t base = eng->output_base_ms;
        AE_UNLOCK();
        return base;
    }
    uint32_t from_frames = (uint32_t)(eng->output_frames_since_seek * 1000u / eng->sample_rate);
    uint32_t pos = eng->output_base_ms + from_frames;
    AE_UNLOCK();
    return pos;
}

/* ── audio_engine_is_playing ──────────────────────────────────────────────── */
bool audio_engine_is_playing(void)
{
    return audio_engine_deck_is_playing(AUDIO_ENGINE_COMPAT_DECK);
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

    /* Keep status consumers on the same audible scratch coordinate as the
     * direct position API. This call is lock-free for an active scratch window. */
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        out->position_ms = audio_engine_position_ms_for_deck(deck);
    }

    return ESP_OK;
}

/* ── audio_engine_get_state / load_progress (P5a) ─────────────────────────── */
ae_state_t audio_engine_get_state(void)
{
    return engine_lifecycle_state(&s_engines[AUDIO_ENGINE_COMPAT_DECK]);
}

uint8_t audio_engine_load_progress(void)
{
    return s_engines[AUDIO_ENGINE_COMPAT_DECK].load_progress;
}

esp_err_t audio_engine_last_error(void)
{
    return s_engines[AUDIO_ENGINE_COMPAT_DECK].last_error;
}

const char *audio_engine_last_error_text(void)
{
    return s_engines[AUDIO_ENGINE_COMPAT_DECK].last_error_text;
}

/* ── audio_engine_set_loop ────────────────────────────────────────────────── */
void audio_engine_set_loop(uint32_t start_ms, uint32_t end_ms)
{
    (void)audio_engine_deck_set_loop(AUDIO_ENGINE_COMPAT_DECK, start_ms, end_ms);
}

/* ── audio_engine_clear_loop ──────────────────────────────────────────────── */
void audio_engine_clear_loop(void)
{
    (void)audio_engine_deck_clear_loop(AUDIO_ENGINE_COMPAT_DECK);
}

/* ── audio_engine_get_loop_state ───────────────────────────────────────────── */
void audio_engine_get_loop_state(bool *active, uint32_t *start_ms, uint32_t *end_ms)
{
    (void)audio_engine_deck_get_loop_state(AUDIO_ENGINE_COMPAT_DECK, active, start_ms, end_ms);
}

#if AE_FW
static bool deck_transport_supported(uint8_t deck);
#endif

esp_err_t audio_engine_deck_set_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    audio_engine_state_t *eng = &s_engines[deck];
    AE_LOCK();
    eng->loop_start_ms = start_ms;
    eng->loop_end_ms   = end_ms;
    eng->loop_active   = true;
    AE_UNLOCK();
    ESP_LOGI(TAG, "Audio loop set: %lu ms to %lu ms", (unsigned long)start_ms, (unsigned long)end_ms);
    return ESP_OK;
}

esp_err_t audio_engine_deck_clear_loop(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    AE_LOCK();
    s_engines[deck].loop_active = false;
    AE_UNLOCK();
    ESP_LOGI(TAG, "Audio loop cleared");
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
    audio_engine_state_t *eng = &s_engines[deck];
    AE_LOCK();
    if (active) *active = eng->loop_active;
    if (start_ms) *start_ms = eng->loop_start_ms;
    if (end_ms) *end_ms = eng->loop_end_ms;
    AE_UNLOCK();
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

esp_err_t audio_engine_deck_load(uint8_t deck,
                                 const char *mp3_path,
                                 const uint32_t *pvbr_400,
                                 uint32_t duration_ms)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    return audio_engine_load_for_deck(deck, mp3_path, pvbr_400, duration_ms);
}

esp_err_t audio_engine_deck_play(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    return audio_engine_play_for_deck(deck);
}

esp_err_t audio_engine_deck_pause(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
#if AE_FW
    if (!deck_transport_supported(deck)) return ESP_ERR_NOT_SUPPORTED;
#endif
    return audio_engine_pause_for_deck(deck);
}

esp_err_t audio_engine_deck_stop(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    return audio_engine_stop_for_deck(deck);
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
    return audio_engine_request_user_seek(deck, position_ms);
}

void audio_engine_deck_set_pitch(uint8_t deck, int16_t raw_pitch)
{
    if (!deck_is_valid(deck)) return;
#if AE_FW
    if (!deck_transport_supported(deck)) return;
#endif
    audio_engine_set_pitch_for_deck(deck, raw_pitch);
}

void audio_engine_deck_set_pitch_percent(uint8_t deck, float percent)
{
    if (!deck_is_valid(deck)) return;
#if AE_FW
    if (!deck_transport_supported(deck)) return;
#endif
    audio_engine_set_pitch_percent_for_deck(deck, percent);
}

void audio_engine_deck_jog_nudge(uint8_t deck, int16_t delta)
{
    if (!deck_is_valid(deck) || delta == 0) return;
    float bend = s_jog_bend[deck] + (float)delta * AE_JOG_BEND_PER_TICK;
    if (bend > AE_JOG_BEND_MAX) bend = AE_JOG_BEND_MAX;
    if (bend < -AE_JOG_BEND_MAX) bend = -AE_JOG_BEND_MAX;
    s_jog_bend[deck] = bend;
}

void audio_engine_deck_set_hold(uint8_t deck, bool held)
{
    if (!deck_is_valid(deck)) return;
    /* Leaving hold cancels any leftover jog nudge so the deck resumes at exactly
     * the fader tempo, not with a stray bend from before the platter was grabbed. */
    if (!held) {
        s_jog_bend[deck] = 0.0f;
    }
    atomic_store_bool(&s_deck_hold[deck], held);
}

bool audio_engine_deck_scratch_begin(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        /* Idempotent active touch, or a fast re-grab before the release fade has
         * completed. The frozen canonical window and scratch head are still
         * valid: cancel the handoff and route the next block back to scratch. */
        ae_scratch_handoff_t phase =
            (ae_scratch_handoff_t)scratch_handoff_load(&s_scratch_handoff[deck]);
        s_scratch_handoff_gain[deck] = 1.0f;
        atomic_store_bool(&s_scratch_capture_reset_ready[deck], false);
        atomic_store_bool(&s_scratch_regrab_requested[deck], true);
        scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_NONE);
        ESP_LOGI(TAG, "scratch re-grab D%u phase=%u", (unsigned)deck,
                 (unsigned)phase);
        return true;
    }

    /* Only engage scratch on a deck that is actually playing. The control task
     * gates on its own shadow play flag, which can lag the engine (e.g. the track
     * hit EOF); the engine state is authoritative, so a stale "playing" shadow
     * never routes a stopped deck to the (empty) scratch source. */
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded || eng->sample_rate == 0u || eng->loading) {
        ESP_LOGW(TAG,
                 "scratch begin D%u rejected: loaded=%u loading=%u rate=%u",
                 (unsigned)deck, eng->loaded ? 1u : 0u,
                 eng->loading ? 1u : 0u, (unsigned)eng->sample_rate);
        return false;
    }
#if AE_FW
    /* CUE can be followed immediately by touch. Give the memory-backed decoder
     * a short control-path window to publish the centered pre-roll rather than
     * engaging a one-sided scratch window or requiring a second touch. */
    for (uint32_t waits = 0u; waits < 60u &&
         eng->timeline_preroll_pending; waits++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#endif
    if (eng->timeline_preroll_pending) {
        ESP_LOGW(TAG, "scratch begin D%u rejected: cue pre-roll pending",
                 (unsigned)deck);
        return false;
    }

    /* Stop new capture batches, then wait for a batch already in progress to
     * publish its final metadata. This is control-path work, never output-hot. */
    atomic_store_bool(&s_scratch_capture_freeze[deck], true);
#if AE_FW
    for (uint32_t waits = 0; waits < 20u &&
         atomic_load_bool(&s_scratch_capture_writing[deck]); waits++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#endif
    if (atomic_load_bool(&s_scratch_capture_writing[deck])) {
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        ESP_LOGW(TAG, "scratch begin D%u rejected: capture writer timeout",
                 (unsigned)deck);
        return false;
    }

    /* Seed the read head at the current playhead within the capture window:
     * head_back = (newest_pos - playhead) in source frames, clamped to the
     * window. The decode task freezes capture while scratching, so `newest`
     * stays fixed under this head. */
    audio_scratch_buffer_t *b = &s_scratch_buf[deck];
    /* The canonical cursors are authoritative. Refresh the compatibility view
     * after the writer has stopped so a touch landing between PCM publication
     * and the batch-end metadata sync cannot observe stale `filled`/write_index. */
    if (timeline_active(deck)) {
        sync_scratch_view_from_timeline(deck, b->newest_pos_ms);
    }
    uint32_t used = audio_scratch_buffer_used(b);
    if (!b->frames || !b->newest_valid || b->sample_rate == 0u || used < 2u) {
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        ESP_LOGW(TAG,
                 "scratch begin D%u rejected: window frames=%u newest=%u rate=%u used=%u",
                 (unsigned)deck, b->frames ? 1u : 0u, b->newest_valid ? 1u : 0u,
                 (unsigned)b->sample_rate, (unsigned)used);
        return false;
    }
    uint64_t back;
    if (timeline_active(deck)) {
        uint64_t write_seq = audio_pcm_timeline_write_seq(&s_pcm_timelines[deck]);
        uint64_t play_seq = audio_pcm_timeline_play_seq(&s_pcm_timelines[deck]);
        if (write_seq == 0u || play_seq >= write_seq) {
            atomic_store_bool(&s_scratch_capture_freeze[deck], false);
            ESP_LOGW(TAG,
                     "scratch begin D%u rejected: no future play=%llu write=%llu",
                     (unsigned)deck, (unsigned long long)play_seq,
                     (unsigned long long)write_seq);
            return false;
        }
        back = (write_seq - 1u) - play_seq;
    } else {
        uint32_t pos = audio_engine_deck_position_ms(deck);
        if (pos > b->newest_pos_ms) {
            atomic_store_bool(&s_scratch_capture_freeze[deck], false);
            ESP_LOGW(TAG, "scratch begin D%u rejected: playhead=%u newest=%u",
                     (unsigned)deck, (unsigned)pos, (unsigned)b->newest_pos_ms);
            return false;
        }
        back = ((uint64_t)(b->newest_pos_ms - pos) * b->sample_rate) / 1000ull;
    }
    if (back >= used) {
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        ESP_LOGW(TAG, "scratch begin D%u rejected: head back=%llu used=%u",
                 (unsigned)deck, (unsigned long long)back, (unsigned)used);
        return false;
    }
    bool started_paused = !eng->playing || eng->paused;
    atomic_store_bool(&s_scratch_started_paused[deck], started_paused);
    atomic_store_bool(&s_scratch_return_paused[deck], false);
    s_scratch_origin_pos_ms[deck] = audio_engine_position_ms_for_deck(deck);
    s_scratch_origin_play_seq[deck] = timeline_active(deck)
        ? (uint32_t)audio_pcm_timeline_play_seq(&s_pcm_timelines[deck]) : 0u;
    float head_back = (float)back;
    float frames_per_tick = AUDIO_SCRATCH_DEFAULT_FRAMES_PER_TICK *
        ((float)b->sample_rate / (float)AE_SCRATCH_MAX_RATE);
    audio_scratch_config(&s_scratch_engine[deck],
                         frames_per_tick,
                         AUDIO_SCRATCH_DEFAULT_RATE_WINDOW,
                         AUDIO_SCRATCH_DEFAULT_SLEW_COEF,
                         AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX,
                         AUDIO_SCRATCH_DEFAULT_HOLD_WINDOWS);
    audio_scratch_seed(&s_scratch_engine[deck], head_back);
    scratch_head_publish(deck);
    /* Cancel any in-flight release handoff and enter steady scratch at full gain.
     * Seed the gain before publishing the phase (release) so the output task never
     * observes NONE with a stale mid-fade gain. */
    s_scratch_handoff_gain[deck] = 1.0f;
    scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_NONE);
    atomic_store_bool(&s_scratch_playing[deck], true);
    return true;
}

void audio_engine_deck_scratch_move(uint8_t deck, int16_t delta)
{
    if (!deck_is_valid(deck)) return;
    audio_scratch_jog(&s_scratch_engine[deck], delta);
}

void audio_engine_deck_scratch_end(uint8_t deck)
{
    if (!deck_is_valid(deck)) return;

    /* No-op if scratch never engaged (begin was declined for a stopped deck):
     * arming the handoff here would flip s_scratch_playing on with no capture. */
    if (!atomic_load_bool(&s_scratch_playing[deck])) {
        return;
    }

    /* Convert the read-head position back to a track position and seek normal
     * playback there. The seek flushes + refills the ring (but not the scratch
     * buffer while the deck is still scratch_playing, so the fade-out can keep
     * reading it). Then arm the cross-fade handoff (4b): the output task fades
     * the scratch tail out and the resumed forward audio in, and only then hands
     * the deck back to the resampler + clears s_scratch_playing. */
    audio_scratch_buffer_t *b = &s_scratch_buf[deck];
    float head_back = scratch_head_snapshot(deck);
    bool return_paused = atomic_load_bool(&s_scratch_started_paused[deck]);

    if (return_paused) {
        if (timeline_active(deck)) {
            (void)audio_pcm_timeline_set_playhead(
                &s_pcm_timelines[deck], s_scratch_origin_play_seq[deck]);
        } else {
            (void)audio_engine_seek_for_deck_reason(
                deck, s_scratch_origin_pos_ms[deck], AE_SEEK_REASON_SCRATCH_RELEASE);
        }
        s_engines[deck].output_base_ms = s_scratch_origin_pos_ms[deck];
        s_engines[deck].output_frames_since_seek = 0u;
#if AE_FW
        audio_resampler_reset(&s_resamplers[deck]);
#endif
        atomic_store_bool(&s_scratch_return_paused[deck], true);
    }

    if (!return_paused && timeline_active(deck) &&
        b->newest_valid && b->sample_rate > 0u) {
        uint32_t frames_back = head_back > 0.0f ? (uint32_t)head_back : 0u;
        if (audio_pcm_timeline_set_playhead_frames_back(&s_pcm_timelines[deck],
                                                        frames_back)) {
            uint32_t target = audio_scratch_track_position_ms(
                b->newest_pos_ms, head_back, b->sample_rate,
                s_engines[deck].loop_active,
                s_engines[deck].loop_start_ms,
                s_engines[deck].loop_end_ms);
            s_engines[deck].output_base_ms = target;
            s_engines[deck].output_frames_since_seek = 0u;
#if AE_FW
            audio_resampler_reset(&s_resamplers[deck]);
#endif
        }
    } else if (!return_paused && b->newest_valid && b->sample_rate > 0u) {
        uint32_t target = audio_scratch_track_position_ms(
            b->newest_pos_ms, head_back, b->sample_rate,
            s_engines[deck].loop_active,
            s_engines[deck].loop_start_ms,
            s_engines[deck].loop_end_ms);
        (void)audio_engine_seek_for_deck_reason(deck, target,
                                                AE_SEEK_REASON_SCRATCH_RELEASE);
    }

    /* Seed the fade gain before publishing the FADE_OUT phase (release) so the
     * output task, on observing FADE_OUT, always sees gain == 1.0. */
    s_scratch_handoff_gain[deck] = 1.0f;
    scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_FADE_OUT);
    /* s_scratch_playing stays true through the handoff; the output task clears it
     * once the fade-in reaches full gain (AE_SCRATCH_HANDOFF_RING). */
}

uint32_t audio_engine_deck_position_ms(uint8_t deck)
{
    if (!deck_is_valid(deck)) return 0;
#if AE_FW
    if (!deck_transport_supported(deck)) return 0;
#endif
    return audio_engine_position_ms_for_deck(deck);
}

bool audio_engine_deck_is_playing(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
#if AE_FW
    if (!deck_transport_supported(deck)) return false;
#endif
    return s_engines[deck].playing && !s_engines[deck].paused;
}

uint16_t audio_engine_get_deck_peak(uint8_t deck)
{
    if (!deck_is_valid(deck)) return 0;
    /* Lock-free read-and-reset (atomic exchange); no audio mutex so this never
     * contends with the LVGL/decode/output tasks. */
    return __atomic_exchange_n(&s_deck_peak[deck], 0u, __ATOMIC_RELAXED);
}

static uint32_t beat_fx_echo_capacity_frames(void)
{
    return (AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE *
            AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS) / 1000u;
}

static int16_t *beat_fx_echo_alloc_buffer(uint32_t frames)
{
#if AE_FW
    return (int16_t *)heap_caps_calloc(frames, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return (int16_t *)calloc(frames, sizeof(int16_t));
#endif
}

static void init_beat_fx_echo_buffers(void)
{
    uint32_t frames = beat_fx_echo_capacity_frames();
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        if (!s_beat_fx_echo_left[deck]) {
            s_beat_fx_echo_left[deck] = beat_fx_echo_alloc_buffer(frames);
        }
        if (!s_beat_fx_echo_right[deck]) {
            s_beat_fx_echo_right[deck] = beat_fx_echo_alloc_buffer(frames);
        }
        audio_delay_fx_init(&s_beat_fx_echo[deck],
                            s_beat_fx_echo_left[deck],
                            s_beat_fx_echo_right[deck],
                            frames,
                            AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE);
        atomic_store_bool(&s_beat_fx_echo_enabled[deck], false);
        atomic_store_u32(&s_beat_fx_echo_delay_ms[deck], 0u);
    }
}

static void init_beat_fx_flanger_buffers(void)
{
    uint32_t frames = audio_flanger_fx_required_frames(
        AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE);
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        if (!s_beat_fx_flanger_left[deck]) {
            s_beat_fx_flanger_left[deck] = beat_fx_echo_alloc_buffer(frames);
        }
        if (!s_beat_fx_flanger_right[deck]) {
            s_beat_fx_flanger_right[deck] = beat_fx_echo_alloc_buffer(frames);
        }
        audio_flanger_fx_init(&s_beat_fx_flanger[deck],
                              s_beat_fx_flanger_left[deck],
                              s_beat_fx_flanger_right[deck],
                              frames,
                              AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE);
        atomic_store_bool(&s_beat_fx_flanger_enabled[deck], false);
    }
}

static uint32_t pad_fx_echo_capacity_frames(void)
{
    return (AUDIO_ENGINE_PAD_FX_ECHO_FALLBACK_SAMPLE_RATE *
            AUDIO_ENGINE_PAD_FX_ECHO_MAX_DELAY_MS) / 1000u;
}

static void init_pad_fx_buffers(void)
{
    uint32_t frames = pad_fx_echo_capacity_frames();
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        if (!s_pad_fx_echo_left[deck]) {
            s_pad_fx_echo_left[deck] = beat_fx_echo_alloc_buffer(frames);
        }
        if (!s_pad_fx_echo_right[deck]) {
            s_pad_fx_echo_right[deck] = beat_fx_echo_alloc_buffer(frames);
        }
        audio_pad_fx_init_with_echo_buffer(&s_pad_fx[deck],
                                           AUDIO_ENGINE_PAD_FX_ECHO_FALLBACK_SAMPLE_RATE,
                                           s_pad_fx_echo_left[deck],
                                           s_pad_fx_echo_right[deck],
                                           frames);
    }
}

/* Allocate the per-deck scratch capture stores (once) and bind each buffer.
 * Stereo, so capacity*2 int16. On PSRAM (~768 KB/deck at the default 4 s @
 * 48 kHz); if an allocation fails the buffer stays unbound and capture is a
 * no-op for that deck — playback is unaffected. */
static void init_scratch_buffers(void)
{
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        if (!s_pcm_timeline_storage[deck]) {
            s_pcm_timeline_storage[deck] =
                beat_fx_echo_alloc_buffer(AE_TIMELINE_CAPACITY_FRAMES * 2u);
        }
        audio_pcm_timeline_init(&s_pcm_timelines[deck],
                                s_pcm_timeline_storage[deck],
                                s_pcm_timeline_storage[deck]
                                    ? AE_TIMELINE_CAPACITY_FRAMES : 0u);
        if (!s_pcm_timeline_storage[deck] && !s_scratch_storage[deck]) {
            s_scratch_storage[deck] =
                beat_fx_echo_alloc_buffer(AE_SCRATCH_CAPACITY_FRAMES * 2u);
        }
        audio_scratch_buffer_init(&s_scratch_buf[deck],
                                  s_pcm_timeline_storage[deck]
                                      ? s_pcm_timeline_storage[deck]
                                      : s_scratch_storage[deck],
                                  s_pcm_timeline_storage[deck]
                                      ? AE_TIMELINE_CAPACITY_FRAMES
                                      : (s_scratch_storage[deck]
                                            ? AE_SCRATCH_CAPACITY_FRAMES : 0u));
        ESP_LOGI(TAG, "PCM timeline D%u: %s, capacity=%u frames",
                 (unsigned)deck,
                 timeline_active(deck) ? "PSRAM canonical" : "legacy fallback",
                 (unsigned)(timeline_active(deck)
                     ? AE_TIMELINE_CAPACITY_FRAMES : AUDIO_PCM_RING_FRAMES));
        audio_scratch_init(&s_scratch_engine[deck]);
        atomic_store_bool(&s_scratch_playing[deck], false);
        atomic_store_bool(&s_scratch_capture_reset_ready[deck], false);
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        atomic_store_bool(&s_scratch_capture_writing[deck], false);
        atomic_store_bool(&s_scratch_abort_seek_requested[deck], false);
        atomic_store_bool(&s_scratch_abort_seek_waiting[deck], false);
        atomic_store_bool(&s_scratch_regrab_requested[deck], false);
        atomic_store_bool(&s_scratch_started_paused[deck], false);
        atomic_store_bool(&s_scratch_return_paused[deck], false);
        atomic_store_bool(&s_pending_pitch_valid[deck], false);
        s_pending_pitch_factor[deck] = 1.0f;
        s_scratch_origin_pos_ms[deck] = 0u;
        s_scratch_origin_play_seq[deck] = 0u;
        __atomic_store_n(&s_scratch_abort_seek_target_ms[deck], 0u,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&s_scratch_head_back_bits[deck], float_to_bits(0.0f),
                         __ATOMIC_RELAXED);
        s_scratch_handoff_gain[deck] = 1.0f;
        scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_NONE);
        s_scratch_ctx_deck[deck] = deck;
    }
}

#if defined(AUDIO_ENGINE_PC_TEST)
void audio_engine_test_record_deck_peak(uint8_t deck, int16_t left, int16_t right)
{
    AE_LOCK();
    record_deck_peak(deck, (audio_mixer_frame_t){ .left = left, .right = right });
    AE_UNLOCK();
}

void audio_engine_test_record_limiter_stats(const audio_mixer_limiter_stats_t *stats)
{
    if (!stats) return;
    AE_LOCK();
    s_limiter_stats.limited_samples += stats->limited_samples;
    s_limiter_stats.positive_overloads += stats->positive_overloads;
    s_limiter_stats.negative_overloads += stats->negative_overloads;
    if (stats->peak_input_abs > s_limiter_stats.peak_input_abs) {
        s_limiter_stats.peak_input_abs = stats->peak_input_abs;
    }
    AE_UNLOCK();
}
#endif

esp_err_t audio_engine_set_channel_volume(uint8_t deck, uint16_t raw_volume)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    if (raw_volume > AUDIO_MIXER_CONTROL_MAX) {
        raw_volume = AUDIO_MIXER_CONTROL_MAX;
    }
    atomic_store_u16(&s_channel_volume[deck], raw_volume);
    return ESP_OK;
}

esp_err_t audio_engine_set_crossfader(uint16_t raw_crossfader)
{
    if (raw_crossfader > AUDIO_MIXER_CONTROL_MAX) {
        raw_crossfader = AUDIO_MIXER_CONTROL_MAX;
    }
    atomic_store_u16(&s_crossfader, raw_crossfader);
    return ESP_OK;
}

esp_err_t audio_engine_set_pregain(uint8_t deck, uint16_t raw_pregain)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    if (raw_pregain > AUDIO_MIXER_CONTROL_MAX) {
        raw_pregain = AUDIO_MIXER_CONTROL_MAX;
    }
    atomic_store_u16(&s_pregain[deck], raw_pregain);
    return ESP_OK;
}

uint16_t audio_engine_get_pregain(uint8_t deck)
{
    if (!deck_is_valid(deck)) return AUDIO_MIXER_CONTROL_CENTER;
    return atomic_load_u16(&s_pregain[deck]);
}

esp_err_t audio_engine_set_master_volume(uint16_t raw_volume)
{
    if (raw_volume > AUDIO_MIXER_CONTROL_MAX) {
        raw_volume = AUDIO_MIXER_CONTROL_MAX;
    }
    atomic_store_u16(&s_master_volume, raw_volume);
    return ESP_OK;
}

uint16_t audio_engine_get_master_volume(void)
{
    return atomic_load_u16(&s_master_volume);
}

esp_err_t audio_engine_set_headphone_mix(uint16_t raw_mix)
{
    if (raw_mix > AUDIO_MIXER_CONTROL_MAX) {
        raw_mix = AUDIO_MIXER_CONTROL_MAX;
    }
    atomic_store_u16(&s_headphone_mix, raw_mix);
    return ESP_OK;
}

uint16_t audio_engine_get_headphone_mix(void)
{
    return atomic_load_u16(&s_headphone_mix);
}

esp_err_t audio_engine_set_headphone_level(uint16_t raw_level)
{
    if (raw_level > AUDIO_MIXER_CONTROL_MAX) {
        raw_level = AUDIO_MIXER_CONTROL_MAX;
    }
    atomic_store_u16(&s_headphone_level, raw_level);
    return ESP_OK;
}

uint16_t audio_engine_get_headphone_level(void)
{
    return atomic_load_u16(&s_headphone_level);
}

esp_err_t audio_engine_set_eq(uint8_t deck, audio_eq_band_t band, uint16_t raw)
{
    if (!deck_is_valid(deck) || band >= AUDIO_EQ_BAND_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_eq_set_band_raw(&s_deck_eq[deck], band, raw);
    return ESP_OK;
}

uint16_t audio_engine_get_eq(uint8_t deck, audio_eq_band_t band)
{
    if (!deck_is_valid(deck) || band >= AUDIO_EQ_BAND_COUNT) {
        return AUDIO_EQ_RAW_CENTER;
    }
    return audio_eq_get_band_raw(&s_deck_eq[deck], band);
}

esp_err_t audio_engine_set_filter(uint8_t deck, uint16_t raw_filter)
{
    if (!deck_is_valid(deck)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (raw_filter > AUDIO_FILTER_RAW_MAX) {
        raw_filter = AUDIO_FILTER_RAW_MAX;
    }
    atomic_store_u16(&s_deck_filter_raw[deck], raw_filter);
    apply_deck_filter_raw(deck);
    return ESP_OK;
}

uint16_t audio_engine_get_filter(uint8_t deck)
{
    if (!deck_is_valid(deck)) {
        return AUDIO_FILTER_RAW_CENTER;
    }
    return atomic_load_u16(&s_deck_filter_raw[deck]);
}

static uint16_t beat_fx_filter_raw_from_depth(uint8_t depth)
{
    if (depth == 0u) {
        return AUDIO_FILTER_RAW_CENTER;
    }
    uint32_t sweep = ((uint32_t)AUDIO_FILTER_RAW_CENTER * (uint32_t)depth + 63u) / 127u;
    if (sweep > AUDIO_FILTER_RAW_CENTER) {
        sweep = AUDIO_FILTER_RAW_CENTER;
    }
    return (uint16_t)(AUDIO_FILTER_RAW_CENTER - sweep);
}

static bool beat_fx_target_includes_deck(audio_engine_beat_fx_target_t target, uint8_t deck)
{
    switch (target) {
    case AUDIO_ENGINE_BEAT_FX_TARGET_CH1:
        return deck == 0u;
    case AUDIO_ENGINE_BEAT_FX_TARGET_CH2:
        return deck == 1u;
    case AUDIO_ENGINE_BEAT_FX_TARGET_BOTH:
        return deck < AUDIO_ENGINE_DECK_COUNT;
    default:
        return false;
    }
}

esp_err_t audio_engine_set_beat_fx_filter(audio_engine_beat_fx_target_t target,
                                          uint8_t depth,
                                          bool enabled)
{
    if (target != AUDIO_ENGINE_BEAT_FX_TARGET_CH1 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_CH2 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_BOTH) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw = beat_fx_filter_raw_from_depth(depth);
    bool active = enabled && depth > 0u;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        bool deck_enabled = active && beat_fx_target_includes_deck(target, deck);
        atomic_store_bool(&s_beat_fx_filter_enabled[deck], deck_enabled);
        audio_filter_set_raw(&s_beat_fx_filter[deck], deck_enabled ? raw : AUDIO_FILTER_RAW_CENTER);
        if (!deck_enabled) {
            audio_filter_reset(&s_beat_fx_filter[deck]);
        }
    }
    return ESP_OK;
}

static uint16_t beat_fx_echo_wet_from_depth(uint8_t depth)
{
    if (depth > 127u) depth = 127u;
    /* sqrt taper: audible repeats early on the knob, 0.70 wet at full. */
    float x = (float)depth / 127.0f;
    return (uint16_t)(22938.0f * sqrtf(x) + 0.5f);
}

static uint16_t beat_fx_echo_feedback_from_depth(uint8_t depth)
{
    if (depth > 127u) depth = 127u;
    /* 0.20 floor (a couple of repeats as soon as the FX engages) to 0.68. */
    float x = (float)depth / 127.0f;
    return (uint16_t)(6554.0f + x * (22282.0f - 6554.0f) + 0.5f);
}

esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                        uint8_t depth,
                                        uint32_t delay_ms,
                                        bool enabled)
{
    if (target != AUDIO_ENGINE_BEAT_FX_TARGET_CH1 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_CH2 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_BOTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (delay_ms == 0u) {
        delay_ms = 1u;
    }
    if (delay_ms > AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS) {
        delay_ms = AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS;
    }

    bool active = enabled && depth > 0u;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        bool deck_enabled = active &&
                            beat_fx_target_includes_deck(target, deck) &&
                            audio_delay_fx_is_allocated(&s_beat_fx_echo[deck]);
        if (active &&
            beat_fx_target_includes_deck(target, deck) &&
            !audio_delay_fx_is_allocated(&s_beat_fx_echo[deck])) {
            ESP_LOGW(TAG, "beat fx echo deck %u buffer not allocated", (unsigned)deck);
        }
        atomic_store_bool(&s_beat_fx_echo_enabled[deck], deck_enabled);
        atomic_store_u32(&s_beat_fx_echo_delay_ms[deck], deck_enabled ? delay_ms : 0u);
        /* No reset on switch-off: audio_delay_fx keeps the tail ringing and
         * the output mixer keeps processing until it decays. */
        audio_delay_fx_configure(&s_beat_fx_echo[deck], &(audio_delay_fx_config_t) {
            .enabled = deck_enabled,
            .delay_ms = delay_ms,
            .wet_q15 = beat_fx_echo_wet_from_depth(depth),
            .feedback_q15 = beat_fx_echo_feedback_from_depth(depth),
        });
    }
    return ESP_OK;
}

static uint16_t beat_fx_flanger_depth_q15(uint8_t depth)
{
    if (depth > 127u) depth = 127u;
    return (uint16_t)(((uint32_t)depth * 32767u) / 127u);
}

esp_err_t audio_engine_set_beat_fx_flanger(audio_engine_beat_fx_target_t target,
                                           uint8_t depth,
                                           uint32_t period_ms,
                                           bool enabled)
{
    if (target != AUDIO_ENGINE_BEAT_FX_TARGET_CH1 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_CH2 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_BOTH) {
        return ESP_ERR_INVALID_ARG;
    }

    bool active = enabled && depth > 0u;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        bool deck_enabled = active &&
                            beat_fx_target_includes_deck(target, deck) &&
                            audio_flanger_fx_is_allocated(&s_beat_fx_flanger[deck]);
        if (active &&
            beat_fx_target_includes_deck(target, deck) &&
            !audio_flanger_fx_is_allocated(&s_beat_fx_flanger[deck])) {
            ESP_LOGW(TAG, "beat fx flanger deck %u buffer not allocated", (unsigned)deck);
        }
        atomic_store_bool(&s_beat_fx_flanger_enabled[deck], deck_enabled);
        audio_flanger_fx_configure(&s_beat_fx_flanger[deck], &(audio_flanger_fx_config_t) {
            .enabled = deck_enabled,
            .period_ms = period_ms,
            .depth_q15 = beat_fx_flanger_depth_q15(depth),
        });
    }
    return ESP_OK;
}

esp_err_t audio_engine_set_pad_fx(uint8_t deck,
                                  audio_pad_fx_mode_t mode,
                                  uint8_t pad,
                                  bool active)
{
    if (!deck_is_valid(deck)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode != AUDIO_PAD_FX_MODE_PAD_FX1 &&
        mode != AUDIO_PAD_FX_MODE_PAD_FX2) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_pad_fx_set(&s_pad_fx[deck], (audio_pad_fx_config_t) {
        .mode = mode,
        .pad = pad,
        .active = active,
    });
    return ESP_OK;
}

esp_err_t audio_engine_set_master_trim(float gain)
{
    if (gain < 0.0f) {
        gain = 0.0f;
    } else if (gain > 1.0f) {
        gain = 1.0f;
    }
    s_master_trim = gain;
    return ESP_OK;
}

float audio_engine_get_master_trim(void)
{
    return s_master_trim;
}

void audio_engine_get_output_gains(float *deck0_gain, float *deck1_gain)
{
    float xf0 = 1.0f;
    float xf1 = 1.0f;
    uint16_t crossfader = atomic_load_u16(&s_crossfader);
    uint16_t channel_volume0 = atomic_load_u16(&s_channel_volume[0]);
    uint16_t channel_volume1 = atomic_load_u16(&s_channel_volume[1]);
    uint16_t pregain0 = atomic_load_u16(&s_pregain[0]);
    uint16_t pregain1 = atomic_load_u16(&s_pregain[1]);
    uint16_t master_volume = atomic_load_u16(&s_master_volume);
    audio_mixer_crossfader_gains(crossfader, &xf0, &xf1);
    if (atomic_load_bool(&s_smart_fader_enabled)) {
        if (crossfader < AUDIO_MIXER_CONTROL_CENTER) {
            xf1 *= xf1;
        } else if (crossfader > AUDIO_MIXER_CONTROL_CENTER) {
            xf0 *= xf0;
        }
    }

    if (deck0_gain) {
        *deck0_gain = audio_mixer_fader_gain(channel_volume0) *
                      pregain_gain_from_raw(pregain0) *
                      xf0 *
                      audio_mixer_fader_gain(master_volume) *
                      s_master_trim;
    }
    if (deck1_gain) {
        *deck1_gain = audio_mixer_fader_gain(channel_volume1) *
                      pregain_gain_from_raw(pregain1) *
                      xf1 *
                      audio_mixer_fader_gain(master_volume) *
                      s_master_trim;
    }
}

esp_err_t audio_engine_toggle_pfl(uint8_t deck)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    atomic_store_bool(&s_pfl_enabled[deck], !atomic_load_bool(&s_pfl_enabled[deck]));
    return ESP_OK;
}

bool audio_engine_get_pfl_enabled(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
    return atomic_load_bool(&s_pfl_enabled[deck]);
}

esp_err_t audio_engine_toggle_smart_cfx(void)
{
    atomic_store_bool(&s_smart_cfx_enabled, !atomic_load_bool(&s_smart_cfx_enabled));
    apply_all_deck_filter_raw();
    return ESP_OK;
}

bool audio_engine_get_smart_cfx_enabled(void)
{
    return atomic_load_bool(&s_smart_cfx_enabled);
}

esp_err_t audio_engine_toggle_smart_fader(void)
{
    atomic_store_bool(&s_smart_fader_enabled, !atomic_load_bool(&s_smart_fader_enabled));
    return ESP_OK;
}

bool audio_engine_get_smart_fader_enabled(void)
{
    return atomic_load_bool(&s_smart_fader_enabled);
}

esp_err_t audio_engine_toggle_master_cue(void)
{
    atomic_store_bool(&s_master_cue_enabled, !atomic_load_bool(&s_master_cue_enabled));
    return ESP_OK;
}

bool audio_engine_get_master_cue_enabled(void)
{
    return atomic_load_bool(&s_master_cue_enabled);
}

void audio_engine_get_mixer_snapshot(audio_engine_mixer_snapshot_t *out_snapshot)
{
    if (!out_snapshot) return;
    float gain0 = 0.0f;
    float gain1 = 0.0f;
    audio_engine_get_output_gains(&gain0, &gain1);
    out_snapshot->channel_volume[0] = atomic_load_u16(&s_channel_volume[0]);
    out_snapshot->channel_volume[1] = atomic_load_u16(&s_channel_volume[1]);
    out_snapshot->crossfader = atomic_load_u16(&s_crossfader);
    out_snapshot->pregain[0] = atomic_load_u16(&s_pregain[0]);
    out_snapshot->pregain[1] = atomic_load_u16(&s_pregain[1]);
    out_snapshot->pregain_gain[0] = pregain_gain_from_raw(out_snapshot->pregain[0]);
    out_snapshot->pregain_gain[1] = pregain_gain_from_raw(out_snapshot->pregain[1]);
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        for (uint8_t band = 0; band < AUDIO_EQ_BAND_COUNT; band++) {
            out_snapshot->eq[deck][band] = audio_eq_get_band_raw(&s_deck_eq[deck], (audio_eq_band_t)band);
        }
        out_snapshot->filter[deck] = atomic_load_u16(&s_deck_filter_raw[deck]);
        out_snapshot->smart_cfx_filter_effective[deck] = atomic_load_u16(&s_deck_filter_effective[deck]);
        out_snapshot->beat_fx_filter_raw[deck] = audio_filter_get_raw(&s_beat_fx_filter[deck]);
        out_snapshot->beat_fx_filter_enabled[deck] = atomic_load_bool(&s_beat_fx_filter_enabled[deck]);
        out_snapshot->beat_fx_echo_enabled[deck] = atomic_load_bool(&s_beat_fx_echo_enabled[deck]);
        out_snapshot->beat_fx_echo_delay_ms[deck] = atomic_load_u32(&s_beat_fx_echo_delay_ms[deck]);
        out_snapshot->beat_fx_echo_allocated[deck] = audio_delay_fx_is_allocated(&s_beat_fx_echo[deck]);
        out_snapshot->pad_fx_active[deck] = audio_pad_fx_is_active(&s_pad_fx[deck]);
        out_snapshot->pad_fx_kind[deck] = audio_pad_fx_kind(&s_pad_fx[deck]);
    }
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        out_snapshot->deck_peak[deck] = atomic_load_u16(&s_deck_peak[deck]);
        out_snapshot->deck_peak_display[deck] = atomic_load_u16(&s_deck_ui_peak[deck]);
        ae_scratch_handoff_t scratch_phase =
            (ae_scratch_handoff_t)scratch_handoff_load(&s_scratch_handoff[deck]);
        bool scratch_position_authoritative =
            atomic_load_bool(&s_scratch_playing[deck]) &&
            (scratch_phase == AE_SCRATCH_HANDOFF_NONE ||
             scratch_phase == AE_SCRATCH_HANDOFF_FADE_OUT);
        out_snapshot->scratch_position_authoritative[deck] =
            scratch_position_authoritative;
        float speed = scratch_position_authoritative ? 0.0f :
            s_engines[deck].pitch_factor * (1.0f + s_jog_bend[deck]) * 1000.0f;
        if (speed < 0.0f) speed = 0.0f;
        if (speed > 65535.0f) speed = 65535.0f;
        out_snapshot->effective_speed_permille[deck] = (uint16_t)(speed + 0.5f);
    }
    out_snapshot->master_trim = s_master_trim;
    out_snapshot->master_volume = atomic_load_u16(&s_master_volume);
    out_snapshot->headphone_mix = atomic_load_u16(&s_headphone_mix);
    out_snapshot->headphone_level = atomic_load_u16(&s_headphone_level);
    out_snapshot->master_cue_enabled = atomic_load_bool(&s_master_cue_enabled);
    out_snapshot->output_gain[0] = gain0;
    out_snapshot->output_gain[1] = gain1;
    out_snapshot->pfl_enabled[0] = atomic_load_bool(&s_pfl_enabled[0]);
    out_snapshot->pfl_enabled[1] = atomic_load_bool(&s_pfl_enabled[1]);
    out_snapshot->smart_cfx_enabled = atomic_load_bool(&s_smart_cfx_enabled);
    out_snapshot->smart_fader_enabled = atomic_load_bool(&s_smart_fader_enabled);
    out_snapshot->limiter = s_limiter_stats;
}

void audio_engine_get_diagnostics_snapshot(audio_engine_diagnostics_snapshot_t *out_snapshot)
{
    if (!out_snapshot) return;
    memset(out_snapshot, 0, sizeof(*out_snapshot));

    AE_LOCK();
    out_snapshot->ring_capacity = timeline_active(0u)
        ? AE_TIMELINE_CAPACITY_FRAMES : AUDIO_PCM_RING_FRAMES;
    out_snapshot->scratch_buffer_capacity = AE_SCRATCH_CAPACITY_FRAMES;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        audio_engine_state_t *eng = &s_engines[deck];
        out_snapshot->deck_active[deck] = eng->playing && !eng->paused;
        out_snapshot->ring_used[deck] = deck_pcm_used(deck);
        out_snapshot->pcm_timeline_active[deck] = timeline_active(deck);
        out_snapshot->pcm_timeline_history[deck] = timeline_active(deck)
            ? audio_pcm_timeline_history_frames(&s_pcm_timelines[deck]) : 0u;
        out_snapshot->pcm_timeline_future[deck] = timeline_active(deck)
            ? audio_pcm_timeline_future_frames(&s_pcm_timelines[deck]) : 0u;
        out_snapshot->pcm_timeline_generation[deck] = timeline_active(deck)
            ? audio_pcm_timeline_generation(&s_pcm_timelines[deck]) : 0u;
        out_snapshot->pcm_underrun_count[deck] = s_pcm_underrun_count[deck];
        out_snapshot->scratch_edge_hit_count[deck] =
            s_scratch_engine[deck].edge_hits;
        out_snapshot->scratch_active[deck] = atomic_load_bool(&s_scratch_playing[deck]);
        out_snapshot->scratch_capture_frozen[deck] =
            atomic_load_bool(&s_scratch_capture_freeze[deck]);
        out_snapshot->scratch_buffer_used[deck] =
            audio_scratch_buffer_used(&s_scratch_buf[deck]);
        out_snapshot->scratch_generation[deck] =
            audio_scratch_buffer_generation(&s_scratch_buf[deck]);
        float head_back = scratch_head_snapshot(deck);
        out_snapshot->scratch_head_back_frames[deck] =
            head_back > 0.0f ? (uint32_t)head_back : 0u;
        out_snapshot->deck_sample_rate[deck] = eng->sample_rate;
        out_snapshot->deck_channels[deck] = (uint8_t)((eng->channels > 0) ? eng->channels : 0);
        out_snapshot->deck_file_bytes[deck] = (uint32_t)eng->file_size;
        out_snapshot->deck_load_progress[deck] =
            (eng->loaded || eng->loading) ? eng->load_progress : 0u;
        out_snapshot->beat_fx_echo_allocated[deck] = audio_delay_fx_is_allocated(&s_beat_fx_echo[deck]);
        out_snapshot->beat_fx_echo_enabled[deck] = atomic_load_bool(&s_beat_fx_echo_enabled[deck]);
        out_snapshot->beat_fx_echo_delay_ms[deck] = atomic_load_u32(&s_beat_fx_echo_delay_ms[deck]);
        out_snapshot->pad_fx_active[deck] = audio_pad_fx_is_active(&s_pad_fx[deck]);
    }
    out_snapshot->limiter = s_limiter_stats;
    monitor_pcm_link_stats_t monitor_stats = { 0 };
    monitor_pcm_link_get_stats(&monitor_stats);
    out_snapshot->usb_headphone_submitted_blocks = monitor_stats.submitted_blocks;
    out_snapshot->usb_headphone_dropped_blocks = monitor_stats.dropped_blocks;
    out_snapshot->usb_headphone_submitted_frames = monitor_stats.submitted_frames;
#if AE_FW
    out_snapshot->output_codec_open = s_output_codec_open;
    out_snapshot->output_sample_rate = s_output_sample_rate;
    out_snapshot->output_late_count = s_diag_output_late.count;
    out_snapshot->output_late_max_us = s_diag_output_late.max_us;
    out_snapshot->output_late_threshold_us = s_diag_output_late.threshold_us;
    out_snapshot->heap_free = esp_get_free_heap_size();
    out_snapshot->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out_snapshot->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
    AE_UNLOCK();
}

esp_err_t audio_engine_set_cue_mode(uint8_t mode)
{
    if (mode > 1) return ESP_ERR_INVALID_ARG;
    s_cue_mode = mode;
    s_headphone_mode = mode ? AUDIO_HEADPHONE_MODE_SPLIT_MONO : AUDIO_HEADPHONE_MODE_MASTER_MONO;
    return ESP_OK;
}

uint8_t audio_engine_get_cue_mode(void)
{
    return s_cue_mode;
}

esp_err_t audio_engine_set_headphone_mode(audio_headphone_mode_t mode)
{
    if (mode > AUDIO_HEADPHONE_MODE_SPLIT_MONO) return ESP_ERR_INVALID_ARG;
    s_headphone_mode = mode;
    s_cue_mode = mode == AUDIO_HEADPHONE_MODE_MASTER_MONO ? 0 : 1;
    return ESP_OK;
}

audio_headphone_mode_t audio_engine_get_headphone_mode(void)
{
    return s_headphone_mode;
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
    audio_engine_state_t *eng = &s_engines[AUDIO_ENGINE_COMPAT_DECK];
    if (!eng->loaded || (!eng->fp && !eng->decoder_open && !eng->file_buf)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!wav_path)                 return ESP_ERR_INVALID_ARG;

    FILE *wav = fopen(wav_path, "wb");
    if (!wav) {
        ESP_LOGE(TAG, "Cannot create WAV: %s", wav_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Write placeholder header; patched with real sizes at end */
    wav_write_header(wav, 44100u, 2u, 0u);

    /* Rewind input, reset decoder */
    if (eng->decoder_open) {
        (void)audio_decoder_seek_frame(&eng->decoder, 0u);
    } else if (eng->format == AUDIO_FORMAT_WAV && eng->wav_ready) {
        ae_wav_seek_to_ms(eng, 0u);
    } else if (eng->fp) {
        rewind(eng->fp);
        mp3dec_init(&eng->dec);
    }
    eng->frames_since_seek = 0u;
    eng->eof               = false;

    int16_t  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    uint32_t pcm_bytes   = 0u;
    uint32_t sample_rate = 0u;
    uint16_t channels    = 2u;

    while (true) {
        AE_LOCK();
        int samples = decode_one_frame(eng, pcm);
        if (samples > 0) eng->frames_since_seek += (uint64_t)samples;
        bool eof = eng->eof;
        AE_UNLOCK();

        if (eof || samples <= 0) break;

        /* Latch format on first real audio frame */
        if (sample_rate == 0u && eng->sample_rate > 0u) {
            sample_rate = eng->sample_rate;
            channels    = (eng->channels == 1) ? 2u : (uint16_t)eng->channels;
        }

        /* Respect optional duration limit */
        if (max_duration_ms > 0u && sample_rate > 0u) {
            uint32_t pos = (uint32_t)(eng->frames_since_seek * 1000u / sample_rate);
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
