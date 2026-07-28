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
#include "audio_eof_policy.h"
#include "audio_start_gate.h"
#include "audio_format.h"
#include "audio_diag.h"
#include "audio_delay_fx.h"
#include "audio_filter.h"
#include "audio_flanger_fx.h"
#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"
#include "audio_fw_task_context.h"
#include "audio_fw_task_plan.h"
#include "audio_keylock.h"
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
#if CONFIG_AUDIO_RECORDER_ENABLED
#include "audio_recorder.h"
#endif
#include "service_log.h"
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

#define AE_DECK_0 0u

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
 * fails, normal playback stays on the small PCM ring and scratch begin declines;
 * deck_core then enters the existing platter-hold degraded mode. */
#define AE_TIMELINE_SECONDS        4u
#define AE_TIMELINE_FORWARD_MS     2000u
#define AE_TIMELINE_MAX_RATE       48000u
#define AE_TIMELINE_CAPACITY_FRAMES (AE_TIMELINE_SECONDS * AE_TIMELINE_MAX_RATE)
static audio_pcm_timeline_t s_pcm_timelines[AUDIO_ENGINE_DECK_COUNT];
static int16_t             *s_pcm_timeline_storage[AUDIO_ENGINE_DECK_COUNT];
/* Sole writer is the output task; diagnostics reads are best-effort snapshots. */
static uint32_t s_pcm_underrun_count[AUDIO_ENGINE_DECK_COUNT];
#if AE_FW
/* Delay the first audible block until the producer has two output blocks of
 * runway. The open bench issue was exactly 512 failed frame pops at startup. */
#define AE_START_PREBUFFER_FRAMES 512u
static bool     s_start_waiting[AUDIO_ENGINE_DECK_COUNT];
static uint32_t s_start_wait_count[AUDIO_ENGINE_DECK_COUNT];
#endif
/* Runway the loop-wrap trim must leave behind, in frames. The decoder needs to
 * reseek, reinit the MP3 decoder and produce its first batch before the output
 * runs out; measured, that gap is 512 frames (11.6 ms at 44.1 kHz), so this is
 * four times the observed need. Frames kept back are past loop_end, hence the
 * cost of raising it is overrun at the loop's first pass. */
#define AE_LOOP_TRIM_MIN_RUNWAY_FRAMES 2048u

/* Loop-wrap trim accounting. The trim runs on the decode task and is otherwise
 * invisible: it withdraws already-published frames and clamps the current
 * batch, and neither shows up in the late-block or underrun counters. Without
 * these, a regression in it can only be found by ear. */
static uint32_t s_loop_trim_wraps[AUDIO_ENGINE_DECK_COUNT];
static uint32_t s_loop_trim_dropped_max[AUDIO_ENGINE_DECK_COUNT];
static uint32_t s_loop_trim_dropped_total[AUDIO_ENGINE_DECK_COUNT];
static uint32_t s_loop_trim_clamped_total[AUDIO_ENGINE_DECK_COUNT];

/* Scratch is a metadata/read-head view over the canonical timeline. It owns no
 * second PCM allocation: normal playback can fall back to s_pcm_rings[], while
 * audible scratch requires the canonical PSRAM store. */
static audio_scratch_buffer_t s_scratch_buf[AUDIO_ENGINE_DECK_COUNT];

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
/* Capture coordination is shared by the decoder, control and output tasks. */
static bool              s_scratch_capture_freeze[AUDIO_ENGINE_DECK_COUNT];
static bool              s_scratch_capture_writing[AUDIO_ENGINE_DECK_COUNT];
static uint32_t          s_scratch_head_back_bits[AUDIO_ENGINE_DECK_COUNT];
#if AE_FW
static uint32_t          s_scratch_handoff_consumed[AUDIO_ENGINE_DECK_COUNT];
#endif
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

    /* Seekable compressed source size/cursor. Firmware reads through the
     * bounded page cache; PC builds use FILE/audio_decoder backends. */
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
    uint32_t pitch_factor_bits;

    bool     loaded;
    bool     playing;
    bool     paused;
    bool     eof;
    /* True only after the output task has consumed every decoded EOF frame.
     * This is deliberately separate from decoder EOF: a short track may be
     * completely decoded while still paused or before its first PLAY. */
    bool     playback_finished;
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
} audio_engine_state_t;

static audio_engine_state_t  s_engines[AUDIO_ENGINE_DECK_COUNT];

static inline audio_pcm_ring_t *pcm_ring_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_pcm_rings[deck];
    }
    return &s_pcm_rings[AE_DECK_0];
}

static inline audio_scratch_buffer_t *scratch_buffer_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_scratch_buf[deck];
    }
    return &s_scratch_buf[AE_DECK_0];
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
static uint32_t         s_master_trim_bits = 0x3F800000u; /* float 1.0f */
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
static uint32_t         s_beat_fx_filter_command[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_beat_fx_filter_applied[AUDIO_ENGINE_DECK_COUNT];
static audio_delay_fx_t s_beat_fx_echo[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_echo_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_echo_right[AUDIO_ENGINE_DECK_COUNT];
static bool             s_beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_beat_fx_echo_mode[AUDIO_ENGINE_DECK_COUNT];
typedef struct {
    uint32_t sequence;
    uint32_t word0;
    uint32_t word1;
} ae_fx_command_t;
static ae_fx_command_t  s_beat_fx_echo_command[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_beat_fx_echo_applied[AUDIO_ENGINE_DECK_COUNT];
static audio_flanger_fx_t s_beat_fx_flanger[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_flanger_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_beat_fx_flanger_right[AUDIO_ENGINE_DECK_COUNT];
static bool             s_beat_fx_flanger_enabled[AUDIO_ENGINE_DECK_COUNT];
static ae_fx_command_t  s_beat_fx_flanger_command[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_beat_fx_flanger_applied[AUDIO_ENGINE_DECK_COUNT];
static audio_pad_fx_state_t s_pad_fx[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_pad_fx_echo_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t         *s_pad_fx_echo_right[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_pad_fx_command[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_pad_fx_applied[AUDIO_ENGINE_DECK_COUNT];
static bool             s_smart_cfx_enabled;
static bool             s_smart_fader_enabled;
/* Transient jog pitch-bend (nudge) per deck: a jog while playing bumps this, the
 * output task adds it on top of pitch_factor and decays it back to 0, so tempo
 * returns to the fader setting when the jog stops. The IEEE-754 bits are atomic:
 * relying on aligned non-torn loads would still be a C data race, and a plain
 * read/modify/write could discard a control-task nudge during output decay.
 * Feel constants — tune on hardware: each jog tick adds *_PER_TICK (clamped to
 * ±*_MAX = a momentary tempo change); the output task multiplies toward 0 by
 * *_DECAY each ~5.8 ms block so tempo springs back to the fader on release. */
#define AE_JOG_BEND_PER_TICK 0.02f
#define AE_JOG_BEND_MAX      0.30f
#define AE_JOG_BEND_DECAY    0.88f
static uint32_t         s_jog_bend_bits[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_pending_pitch_factor_bits[AUDIO_ENGINE_DECK_COUNT];
static bool             s_pending_pitch_valid[AUDIO_ENGINE_DECK_COUNT];

/* Platter-hold (vinyl mode Phase 1): while the jog platter top is touched during
 * playback, deck_core sets this so the deck output is silenced and its position
 * frozen (an output-level mute, the logical play state stays "playing" for LEDs).
 * Releasing clears it and forward playback resumes instantly from wherever the
 * position was scrubbed to. Atomic because the control task writes it while the
 * output task reads it. */
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

#if AE_FW
static bool pop_deck_source(void *ctx, audio_mixer_frame_t *out_frame)
{
    uint8_t deck = ctx ? *(const uint8_t *)ctx : AE_DECK_0;
    if (deck >= AUDIO_ENGINE_DECK_COUNT) deck = AE_DECK_0;
    if (timeline_active(deck)) {
        bool ok = audio_pcm_timeline_pop(&s_pcm_timelines[deck], out_frame);
        if (!ok) s_pcm_underrun_count[deck]++;
        return ok;
    }
    bool ok = audio_pcm_ring_pop(&s_pcm_rings[deck], out_frame);
    if (!ok) s_pcm_underrun_count[deck]++;
    return ok;
}
#endif

static uint32_t deck_pcm_used(uint8_t deck)
{
    return timeline_active(deck)
        ? audio_pcm_timeline_future_frames(&s_pcm_timelines[deck])
        : audio_pcm_ring_used(&s_pcm_rings[deck]);
}

#if AE_FW
static void complete_eof_drain_if_ready(uint8_t deck);
#endif

#if AE_FW
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
#endif

static void deck_pcm_reset(uint8_t deck)
{
    audio_pcm_ring_reset(&s_pcm_rings[deck]);
    if (timeline_active(deck)) audio_pcm_timeline_reset(&s_pcm_timelines[deck]);
}

#if AE_FW
static bool deck_pcm_push(uint8_t deck, int16_t left, int16_t right)
{
    return timeline_active(deck)
        ? audio_pcm_timeline_push(&s_pcm_timelines[deck], left, right)
        : audio_pcm_ring_push(&s_pcm_rings[deck], left, right);
}

/* Withdraw already-published frames the loop just made unreachable. Both stores
 * clamp to what playback has not consumed, so this can never claw back audio
 * that is already on its way out. */
static uint32_t deck_pcm_drop_newest(uint8_t deck, uint32_t frames)
{
    return timeline_active(deck)
        ? audio_pcm_timeline_drop_newest(&s_pcm_timelines[deck], frames)
        : audio_pcm_ring_drop_newest(&s_pcm_rings[deck], frames);
}
#endif

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

static void engine_pitch_store(uint8_t deck, float factor)
{
    __atomic_store_n(&s_engines[deck].pitch_factor_bits,
                     float_to_bits(factor), __ATOMIC_RELEASE);
}

static float engine_pitch_load(uint8_t deck)
{
    return bits_to_float(__atomic_load_n(&s_engines[deck].pitch_factor_bits,
                                         __ATOMIC_ACQUIRE));
}

static void pending_pitch_store(uint8_t deck, float factor)
{
    __atomic_store_n(&s_pending_pitch_factor_bits[deck],
                     float_to_bits(factor), __ATOMIC_RELEASE);
}

#if AE_FW
static float pending_pitch_load(uint8_t deck)
{
    return bits_to_float(__atomic_load_n(&s_pending_pitch_factor_bits[deck],
                                         __ATOMIC_ACQUIRE));
}
#endif

static void jog_bend_store(uint8_t deck, float bend)
{
    __atomic_store_n(&s_jog_bend_bits[deck], float_to_bits(bend),
                     __ATOMIC_RELEASE);
}

static float jog_bend_load(uint8_t deck)
{
    return bits_to_float(__atomic_load_n(&s_jog_bend_bits[deck],
                                         __ATOMIC_ACQUIRE));
}

static void jog_bend_add(uint8_t deck, float delta)
{
    uint32_t expected = __atomic_load_n(&s_jog_bend_bits[deck], __ATOMIC_ACQUIRE);
    for (;;) {
        float bend = bits_to_float(expected);
        if (!isfinite(bend)) bend = 0.0f;
        bend += delta;
        if (bend > AE_JOG_BEND_MAX) bend = AE_JOG_BEND_MAX;
        if (bend < -AE_JOG_BEND_MAX) bend = -AE_JOG_BEND_MAX;
        uint32_t desired = float_to_bits(bend);
        if (__atomic_compare_exchange_n(&s_jog_bend_bits[deck], &expected,
                                        desired, false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return;
        }
    }
}

#if AE_FW
static void jog_bend_decay(uint8_t deck)
{
    uint32_t expected = __atomic_load_n(&s_jog_bend_bits[deck], __ATOMIC_ACQUIRE);
    for (;;) {
        float bend = bits_to_float(expected);
        if (!isfinite(bend)) bend = 0.0f;
        bend *= AE_JOG_BEND_DECAY;
        if (bend < 0.0005f && bend > -0.0005f) bend = 0.0f;
        uint32_t desired = float_to_bits(bend);
        if (__atomic_compare_exchange_n(&s_jog_bend_bits[deck], &expected,
                                        desired, false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return;
        }
    }
}
#endif

static void master_trim_store(float gain)
{
    __atomic_store_n(&s_master_trim_bits, float_to_bits(gain), __ATOMIC_RELEASE);
}

static float master_trim_load(void)
{
    return bits_to_float(__atomic_load_n(&s_master_trim_bits, __ATOMIC_ACQUIRE));
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

/* Effect configuration is produced by the deck/control task and consumed by
 * the real-time output task.  Single-word commands are atomic directly; wider
 * commands use a non-spinning sequence snapshot made entirely from 32-bit
 * atomics (native on ESP32-P4).  The output task skips an in-progress update
 * and retries next block rather than waiting on a preempted lower-priority
 * producer. */
#define AE_FILTER_CMD_ENABLED       (1u << 16)
#define AE_FX_CMD_DELAY_MODE        (1u << 30)
#define AE_FX_CMD_ENABLED           (1u << 31)

static uint32_t pack_filter_command(uint16_t raw, bool enabled)
{
    return (uint32_t)raw | (enabled ? AE_FILTER_CMD_ENABLED : 0u);
}

static uint32_t fx_command_publish(ae_fx_command_t *command,
                                   uint32_t word0,
                                   uint32_t word1)
{
    uint32_t sequence = __atomic_load_n(&command->sequence, __ATOMIC_RELAXED);
    if ((sequence & 1u) != 0u) sequence++;
    __atomic_store_n(&command->sequence, sequence + 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&command->word0, word0, __ATOMIC_RELAXED);
    __atomic_store_n(&command->word1, word1, __ATOMIC_RELAXED);
    sequence += 2u;
    __atomic_store_n(&command->sequence, sequence, __ATOMIC_RELEASE);
    return sequence;
}

static bool fx_command_snapshot(const ae_fx_command_t *command,
                                uint32_t *out_sequence,
                                uint32_t *out_word0,
                                uint32_t *out_word1)
{
    uint32_t before = __atomic_load_n(&command->sequence, __ATOMIC_ACQUIRE);
    if ((before & 1u) != 0u) return false;
    uint32_t word0 = __atomic_load_n(&command->word0, __ATOMIC_RELAXED);
    uint32_t word1 = __atomic_load_n(&command->word1, __ATOMIC_RELAXED);
    uint32_t after = __atomic_load_n(&command->sequence, __ATOMIC_ACQUIRE);
    if (before != after || (after & 1u) != 0u) return false;
    if (out_sequence) *out_sequence = after;
    if (out_word0) *out_word0 = word0;
    if (out_word1) *out_word1 = word1;
    return true;
}

static uint32_t publish_echo_command(uint8_t deck,
                                     const audio_delay_fx_config_t *config)
{
    uint32_t word0 = config ? (config->delay_ms & 0x3FFu) : 0u;
    uint32_t word1 = 0u;
    if (config) {
        if (config->enabled) word0 |= AE_FX_CMD_ENABLED;
        if (config->mode == AUDIO_DELAY_FX_MODE_DELAY) word0 |= AE_FX_CMD_DELAY_MODE;
        word1 = (uint32_t)config->wet_q15 |
                ((uint32_t)config->feedback_q15 << 16);
    }
    return fx_command_publish(&s_beat_fx_echo_command[deck], word0, word1);
}

static void read_echo_command_status(uint8_t deck,
                                     bool *out_enabled,
                                     uint32_t *out_delay_ms,
                                     audio_delay_fx_mode_t *out_mode)
{
    /* All status fields live in the same native 32-bit command word, so a
     * low-rate UI/web snapshot sees either the old or new state as one
     * coherent value even if the producer is publishing concurrently. */
    uint32_t word0 = __atomic_load_n(&s_beat_fx_echo_command[deck].word0,
                                     __ATOMIC_ACQUIRE);
    bool enabled = (word0 & AE_FX_CMD_ENABLED) != 0u;
    if (out_enabled) *out_enabled = enabled;
    if (out_delay_ms) *out_delay_ms = enabled ? (word0 & 0x3FFu) : 0u;
    if (out_mode) {
        *out_mode = (word0 & AE_FX_CMD_DELAY_MODE) != 0u
            ? AUDIO_DELAY_FX_MODE_DELAY
            : AUDIO_DELAY_FX_MODE_ECHO;
    }
}

static bool snapshot_echo_command(uint8_t deck,
                                  uint32_t *out_sequence,
                                  audio_delay_fx_config_t *out_config)
{
    uint32_t word0 = 0u, word1 = 0u;
    if (!out_config || !fx_command_snapshot(&s_beat_fx_echo_command[deck],
                                             out_sequence, &word0, &word1)) {
        return false;
    }
    *out_config = (audio_delay_fx_config_t) {
        .enabled = (word0 & AE_FX_CMD_ENABLED) != 0u,
        .mode = (word0 & AE_FX_CMD_DELAY_MODE) != 0u
            ? AUDIO_DELAY_FX_MODE_DELAY
            : AUDIO_DELAY_FX_MODE_ECHO,
        .delay_ms = word0 & 0x3FFu,
        .wet_q15 = (uint16_t)(word1 & 0xFFFFu),
        .feedback_q15 = (uint16_t)(word1 >> 16),
    };
    return true;
}

static uint32_t publish_flanger_command(uint8_t deck,
                                        const audio_flanger_fx_config_t *config)
{
    uint32_t word0 = config ? config->period_ms : 0u;
    uint32_t word1 = config ? (uint32_t)config->depth_q15 : 0u;
    if (config && config->enabled) word1 |= AE_FX_CMD_ENABLED;
    return fx_command_publish(&s_beat_fx_flanger_command[deck], word0, word1);
}

#if AE_FW
static bool snapshot_flanger_command(uint8_t deck,
                                     uint32_t *out_sequence,
                                     audio_flanger_fx_config_t *out_config)
{
    uint32_t word0 = 0u, word1 = 0u;
    if (!out_config || !fx_command_snapshot(&s_beat_fx_flanger_command[deck],
                                             out_sequence, &word0, &word1)) {
        return false;
    }
    *out_config = (audio_flanger_fx_config_t) {
        .enabled = (word1 & AE_FX_CMD_ENABLED) != 0u,
        .period_ms = word0,
        .depth_q15 = (uint16_t)(word1 & 0xFFFFu),
    };
    return true;
}
#endif

static uint32_t pack_pad_fx_command(audio_pad_fx_config_t config)
{
    return (config.active ? 1u : 0u) |
           (((uint32_t)config.mode & 0x3u) << 1) |
           ((uint32_t)config.pad << 3);
}

static audio_pad_fx_config_t unpack_pad_fx_command(uint32_t packed)
{
    return (audio_pad_fx_config_t) {
        .active = (packed & 1u) != 0u,
        .mode = (audio_pad_fx_mode_t)((packed >> 1) & 0x3u),
        .pad = (uint8_t)((packed >> 3) & 0xFFu),
    };
}

static audio_pad_fx_kind_t pad_fx_kind_from_command(uint32_t packed)
{
    audio_pad_fx_config_t config = unpack_pad_fx_command(packed);
    if (!config.active) return AUDIO_PAD_FX_KIND_NONE;
    if (config.pad < 2u) return AUDIO_PAD_FX_KIND_FILTER;
    if (config.pad < 4u) return AUDIO_PAD_FX_KIND_ECHO;
    return AUDIO_PAD_FX_KIND_NONE;
}

#if AE_FW
static void audio_output_apply_master_tempo_commands(void)
{
    for (uint8_t deck = 0u; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        const uint32_t epoch = __atomic_load_n(
            &s_master_tempo_command_epoch[deck], __ATOMIC_ACQUIRE);
        if (epoch == s_master_tempo_applied_epoch[deck]) {
            continue;
        }
        /* Output task is the sole owner of keylock/resampler DSP mutation. The
         * command takes effect exactly at an audio-block boundary. */
        s_keylocks[deck].initialized = false;
        audio_resampler_reset(&s_resamplers[deck]);
        s_master_tempo_applied_epoch[deck] = epoch;
    }
}

static void audio_output_apply_pending_fx_commands(void)
{
    for (uint8_t deck = 0u; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        uint32_t filter_command = __atomic_load_n(
            &s_beat_fx_filter_command[deck], __ATOMIC_ACQUIRE);
        if (filter_command != s_beat_fx_filter_applied[deck]) {
            bool enabled = (filter_command & AE_FILTER_CMD_ENABLED) != 0u;
            uint16_t raw = (uint16_t)(filter_command & 0xFFFFu);
            audio_filter_set_raw(&s_beat_fx_filter[deck], raw);
            if (!enabled) {
                audio_filter_reset(&s_beat_fx_filter[deck]);
            }
            s_beat_fx_filter_applied[deck] = filter_command;
        }

        uint32_t echo_sequence = 0u;
        audio_delay_fx_config_t echo_config;
        if (snapshot_echo_command(deck, &echo_sequence, &echo_config) &&
            echo_sequence != s_beat_fx_echo_applied[deck]) {
            audio_delay_fx_config_t config = echo_config;
            audio_delay_fx_configure(&s_beat_fx_echo[deck], &config);
            s_beat_fx_echo_applied[deck] = echo_sequence;
        }

        uint32_t flanger_sequence = 0u;
        audio_flanger_fx_config_t flanger_config;
        if (snapshot_flanger_command(deck, &flanger_sequence, &flanger_config) &&
            flanger_sequence != s_beat_fx_flanger_applied[deck]) {
            audio_flanger_fx_config_t config = flanger_config;
            audio_flanger_fx_configure(&s_beat_fx_flanger[deck], &config);
            s_beat_fx_flanger_applied[deck] = flanger_sequence;
        }

        uint32_t pad_command = __atomic_load_n(
            &s_pad_fx_command[deck], __ATOMIC_ACQUIRE);
        if (pad_command != s_pad_fx_applied[deck]) {
            audio_pad_fx_set(&s_pad_fx[deck], unpack_pad_fx_command(pad_command));
            s_pad_fx_applied[deck] = pad_command;
        }
    }
}
#endif

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
#if AE_FW
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
#endif

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

static void decay_idle_deck_ui_peaks(void)
{
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        record_deck_ui_peak(deck, 0u);
    }
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
static audio_keylock_t s_keylocks[AUDIO_ENGINE_DECK_COUNT];
static bool s_master_tempo_enabled[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_master_tempo_command_epoch[AUDIO_ENGINE_DECK_COUNT];
static uint32_t         s_master_tempo_applied_epoch[AUDIO_ENGINE_DECK_COUNT];
static uint32_t s_keylock_generation[AUDIO_ENGINE_DECK_COUNT];

static audio_resampler_state_t *resampler_for_deck(uint8_t deck)
{
    if (deck < AUDIO_ENGINE_DECK_COUNT) {
        return &s_resamplers[deck];
    }
    return &s_resamplers[AE_DECK_0];
}

static bool keylock_timeline_read(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    uint8_t deck = ctx ? *(const uint8_t *)ctx : AE_DECK_0;
    return deck < AUDIO_ENGINE_DECK_COUNT &&
           audio_pcm_timeline_read(&s_pcm_timelines[deck], seq, out);
}

static bool ae_keylock_render_cb(void *ctx, float tempo_factor,
                                 float rate_ratio,
                                 audio_mixer_frame_t *out,
                                 uint32_t *out_consumed)
{
    uint8_t deck = ctx ? *(const uint8_t *)ctx : AE_DECK_0;
    if (deck >= AUDIO_ENGINE_DECK_COUNT || !timeline_active(deck) || !out) return false;
    audio_pcm_timeline_t *timeline = &s_pcm_timelines[deck];
    uint32_t generation = audio_pcm_timeline_generation(timeline);
    if (!s_keylocks[deck].initialized || s_keylock_generation[deck] != generation) {
        audio_keylock_reset(&s_keylocks[deck], audio_pcm_timeline_play_seq(timeline));
        s_keylock_generation[deck] = generation;
    }
    audio_keylock_configure(&s_keylocks[deck], tempo_factor, rate_ratio);
    uint64_t play_seq = audio_pcm_timeline_play_seq(timeline);
    if (!audio_keylock_next(&s_keylocks[deck], keylock_timeline_read, ctx,
                            out, out_consumed, &play_seq)) return false;
    return audio_pcm_timeline_set_playhead(timeline, play_seq);
}

static void complete_eof_drain_if_ready(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT) return;
    audio_engine_state_t *eng = &s_engines[deck];
    audio_eof_policy_snapshot_t snapshot = {
        .decoder_eof = atomic_load_bool(&eng->eof),
        .playback_finished = atomic_load_bool(&eng->playback_finished),
        .playing = atomic_load_bool(&eng->playing),
        .paused = atomic_load_bool(&eng->paused),
        .output_blocked = atomic_load_bool(&s_deck_hold[deck]) ||
                          atomic_load_bool(&s_scratch_playing[deck]) ||
                          atomic_load_bool(&s_scratch_abort_seek_waiting[deck]),
        .pending_frames = deck_pcm_used(deck),
    };
    if (!audio_eof_policy_should_finish(&snapshot)) return;

    /* EOF belongs to the producer; natural completion belongs to the consumer.
     * Re-check after taking the engine lock so a seek cannot be mistaken for a
     * drained track while the decision is being committed. */
    AE_LOCK();
    snapshot.decoder_eof = atomic_load_bool(&eng->eof);
    snapshot.playback_finished = atomic_load_bool(&eng->playback_finished);
    snapshot.playing = atomic_load_bool(&eng->playing);
    snapshot.paused = atomic_load_bool(&eng->paused);
    snapshot.output_blocked = atomic_load_bool(&s_deck_hold[deck]) ||
                              atomic_load_bool(&s_scratch_playing[deck]) ||
                              atomic_load_bool(&s_scratch_abort_seek_waiting[deck]);
    snapshot.pending_frames = deck_pcm_used(deck);
    if (audio_eof_policy_should_finish(&snapshot)) {
        atomic_store_bool(&eng->playback_finished, true);
        atomic_store_bool(&eng->playing, false);
        audio_resampler_reset(&s_resamplers[deck]);
        s_keylocks[deck].initialized = false;
        ESP_LOGI(TAG, "EOF drain complete D%u", (unsigned)deck);
    }
    AE_UNLOCK();
}

static void apply_pending_pitch(uint8_t deck)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT ||
        !__atomic_exchange_n(&s_pending_pitch_valid[deck], false,
                             __ATOMIC_ACQ_REL)) {
        return;
    }
    engine_pitch_store(deck, pending_pitch_load(deck));
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
         (atomic_load_bool(&eng->playing) && !atomic_load_bool(&eng->paused)))) return true;
    bool playing = atomic_load_bool(&eng->playing) &&
                   !atomic_load_bool(&eng->paused);
    if (!playing) return false;
    if (atomic_load_bool(&s_start_waiting[deck])) {
        uint32_t future = deck_pcm_used(deck);
        if (!audio_start_gate_ready(future, AE_START_PREBUFFER_FRAMES,
                                    atomic_load_bool(&eng->eof))) {
            return false;
        }
        atomic_store_bool(&s_start_waiting[deck], false);
        ESP_LOGI(TAG, "startup gate D%u released with %u future frames",
                 (unsigned)deck + 1u, (unsigned)future);
    }
    return true;
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

#if AE_FW
static uint16_t ae_wav_rd_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ae_wav_rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool ae_fw_read_exact(audio_fw_preload_t *fw,
                             size_t offset,
                             void *dst,
                             size_t bytes)
{
    return fw && audio_fw_preload_read_at(fw, offset, dst, bytes) == bytes;
}

static esp_err_t ae_wav_init_from_cache(audio_engine_state_t *eng,
                                        audio_fw_preload_t *fw)
{
    uint8_t header[12];
    if (!eng || !fw || eng->file_size < sizeof(header) ||
        !ae_fw_read_exact(fw, 0u, header, sizeof(header))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_format_detect_header(header, sizeof(header)) != AUDIO_FORMAT_WAV) {
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
        uint8_t chunk[8];
        if (!ae_fw_read_exact(fw, pos, chunk, sizeof(chunk))) return ESP_FAIL;
        uint32_t chunk_size = ae_wav_rd_u32le(chunk + 4);
        size_t payload = pos + 8u;
        size_t padded_size = (size_t)chunk_size + (size_t)(chunk_size & 1u);
        if (payload > eng->file_size || padded_size > eng->file_size - payload) {
            return ESP_FAIL;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk_size < sizeof(fmt) ||
                !ae_fw_read_exact(fw, payload, fmt, sizeof(fmt))) {
                return ESP_FAIL;
            }
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

    if (!have_fmt || !have_data || audio_format != 1u) return ESP_ERR_NOT_SUPPORTED;
    if ((channels != 1u && channels != 2u) || bits_per_sample != 16u) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (sample_rate == 0u || block_align == 0u ||
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
    atomic_store_bool(&eng->eof, eng->wav_total_frames == 0u);
    if (eng->duration_ms == 0u) {
        eng->duration_ms = (uint32_t)((eng->wav_total_frames * 1000ull) /
                                      (uint64_t)sample_rate);
    }
    ESP_LOGI(TAG, "WAV cache: %u Hz, %u ch, %u frames",
             (unsigned)sample_rate, (unsigned)channels,
             (unsigned)eng->wav_total_frames);
    return ESP_OK;
}
#endif

static void ae_wav_seek_to_ms(audio_engine_state_t *eng, uint32_t position_ms)
{
    if (!eng || !eng->wav_ready || eng->sample_rate == 0u || eng->wav_block_align == 0u) return;
    uint64_t frame = ((uint64_t)position_ms * (uint64_t)eng->sample_rate) / 1000ull;
    if (frame > eng->wav_total_frames) frame = eng->wav_total_frames;
    eng->wav_current_frame = frame;
    eng->wav_data_pos = eng->wav_data_offset + (size_t)(frame * eng->wav_block_align);
    eng->file_pos = eng->wav_data_pos;
    atomic_store_bool(&eng->eof, frame >= eng->wav_total_frames);
}

static int ae_wav_decode_one_frame(audio_engine_state_t *eng,
#if AE_FW
                                   audio_fw_preload_t *fw,
#endif
                                   int16_t out_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2])
{
    if (!eng || !eng->wav_ready || atomic_load_bool(&eng->eof) || eng->wav_block_align == 0u) return 0;
    if (eng->wav_current_frame >= eng->wav_total_frames) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }

    uint64_t frames_left64 = eng->wav_total_frames - eng->wav_current_frame;
    size_t frames = frames_left64 > (uint64_t)MINIMP3_MAX_SAMPLES_PER_FRAME
        ? (size_t)MINIMP3_MAX_SAMPLES_PER_FRAME : (size_t)frames_left64;
    size_t data_end = eng->wav_data_offset + eng->wav_data_size;
    if (eng->wav_data_pos >= data_end) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }
    size_t bytes_left = data_end - eng->wav_data_pos;
    size_t frames_available = bytes_left / eng->wav_block_align;
    if (frames > frames_available) frames = frames_available;
    if (frames == 0u) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }

#if AE_FW
    uint8_t raw[MINIMP3_MAX_SAMPLES_PER_FRAME * 4u];
    size_t raw_bytes = frames * eng->wav_block_align;
    if (!ae_fw_read_exact(fw, eng->wav_data_pos, raw, raw_bytes)) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t *sample = raw + i * eng->wav_block_align;
        if (eng->channels == 1) {
            int16_t value = (int16_t)ae_wav_rd_u16le(sample);
            out_pcm[i * 2u] = value;
            out_pcm[i * 2u + 1u] = value;
        } else {
            out_pcm[i * 2u] = (int16_t)ae_wav_rd_u16le(sample);
            out_pcm[i * 2u + 1u] = (int16_t)ae_wav_rd_u16le(sample + 2u);
        }
    }
#else
    (void)out_pcm;
    return 0;
#endif

    eng->wav_current_frame += frames;
    eng->wav_data_pos += frames * eng->wav_block_align;
    eng->file_pos = eng->wav_data_pos;
    if (eng->wav_current_frame >= eng->wav_total_frames) atomic_store_bool(&eng->eof, true);
    return (int)frames;
}

#if AE_FW
/* Firmware FLAC uses dr_flac callbacks over the same bounded cache as MP3/WAV. */
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

static size_t ae_flac_cache_read(void *user, void *out, size_t bytes)
{
    return audio_fw_preload_stream_read((audio_fw_preload_t *)user, out, bytes);
}

static drflac_bool32 ae_flac_cache_seek(void *user, int offset,
                                        drflac_seek_origin origin)
{
    int std_origin;
    if (origin == DRFLAC_SEEK_SET) {
        std_origin = SEEK_SET;
    } else if (origin == DRFLAC_SEEK_CUR) {
        std_origin = SEEK_CUR;
    } else if (origin == DRFLAC_SEEK_END) {
        std_origin = SEEK_END;
    } else {
        return DRFLAC_FALSE;
    }
    return audio_fw_preload_stream_seek((audio_fw_preload_t *)user,
                                        (int64_t)offset, std_origin)
        ? DRFLAC_TRUE : DRFLAC_FALSE;
}

static drflac_bool32 ae_flac_cache_tell(void *user, drflac_int64 *cursor)
{
    if (!user || !cursor) return DRFLAC_FALSE;
    *cursor = (drflac_int64)audio_fw_preload_stream_tell(
        (const audio_fw_preload_t *)user);
    return DRFLAC_TRUE;
}

static esp_err_t ae_flac_init_from_cache(audio_engine_state_t *eng,
                                         audio_fw_preload_t *fw)
{
    if (!eng || !fw || eng->file_size == 0u) return ESP_ERR_INVALID_ARG;
    fw->stream_pos = 0u;
    drflac_allocation_callbacks cb = {
        .pUserData = NULL,
        .onMalloc = ae_flac_psram_malloc,
        .onRealloc = ae_flac_psram_realloc,
        .onFree = ae_flac_psram_free,
    };
    drflac *flac = drflac_open(ae_flac_cache_read,
                               ae_flac_cache_seek,
                               ae_flac_cache_tell,
                               fw,
                               &cb);
    if (!flac) {
        ESP_LOGE(TAG, "drflac_open cache failed (size=%u)", (unsigned)eng->file_size);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (flac->channels != 1u && flac->channels != 2u) {
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
    atomic_store_bool(&eng->eof, flac->totalPCMFrameCount == 0u);
    ESP_LOGI(TAG, "FLAC cache: %u Hz, %u ch, %u bps, %llu frames",
             (unsigned)flac->sampleRate, (unsigned)flac->channels,
             (unsigned)flac->bitsPerSample,
             (unsigned long long)flac->totalPCMFrameCount);
    return ESP_OK;
}

static void ae_flac_seek_to_ms(audio_engine_state_t *eng, uint32_t position_ms)
{
    if (!eng || !eng->flac_ready || !eng->flac || eng->sample_rate == 0u) return;
    drflac *flac = (drflac *)eng->flac;
    uint64_t frame = ((uint64_t)position_ms * (uint64_t)eng->sample_rate) / 1000ull;
    if (frame > flac->totalPCMFrameCount) frame = flac->totalPCMFrameCount;
    (void)drflac_seek_to_pcm_frame(flac, (drflac_uint64)frame);
    atomic_store_bool(&eng->eof, frame >= flac->totalPCMFrameCount);
}

static int ae_flac_decode_one_frame(audio_engine_state_t *eng,
                                    int16_t out_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2])
{
    if (!eng || !eng->flac_ready || !eng->flac || atomic_load_bool(&eng->eof)) return 0;
    drflac *flac = (drflac *)eng->flac;
    const uint8_t channels = (uint8_t)eng->channels;
    int16_t scratch[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    drflac_uint64 got = drflac_read_pcm_frames_s16(
        flac, MINIMP3_MAX_SAMPLES_PER_FRAME, scratch);
    if (got == 0u) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }
    for (size_t i = 0; i < (size_t)got; ++i) {
        if (channels == 1u) {
            out_pcm[i * 2u] = scratch[i];
            out_pcm[i * 2u + 1u] = scratch[i];
        } else {
            out_pcm[i * 2u] = scratch[i * channels];
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
    uint8_t deck,
#endif
    int16_t out_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2])
{
    if (eng->decoder_open) {
        if (atomic_load_bool(&eng->eof)) return 0;
        size_t frames_read = 0u;
        esp_err_t rc = audio_decoder_read_pcm_s16(&eng->decoder,
                                                  out_pcm,
                                                  MINIMP3_MAX_SAMPLES_PER_FRAME,
                                                  &frames_read);
        if (rc != ESP_OK || frames_read == 0u) {
            atomic_store_bool(&eng->eof, true);
            return 0;
        }
        if (eng->sample_rate == 0u && eng->decoder.info.sample_rate > 0u) {
            eng->sample_rate = eng->decoder.info.sample_rate;
            eng->channels = eng->decoder.info.channels;
        }
        return (int)frames_read;
    }

    if (eng->format == AUDIO_FORMAT_WAV) {
        return ae_wav_decode_one_frame(eng,
#if AE_FW
                                       fw,
#endif
                                       out_pcm);
    }
#if AE_FW
    if (eng->format == AUDIO_FORMAT_FLAC) {
        return ae_flac_decode_one_frame(eng, out_pcm);
    }

    if (!fw || !fw->load_done || atomic_load_bool(&eng->eof) ||
        eng->file_pos >= eng->file_size) {
        if (eng->file_pos >= eng->file_size) atomic_store_bool(&eng->eof, true);
        return 0;
    }

    uint8_t input[4096];
    size_t bytes_left = eng->file_size - eng->file_pos;
    size_t wanted = bytes_left < sizeof(input) ? bytes_left : sizeof(input);
    size_t got = audio_fw_preload_read_at(fw, eng->file_pos, input, wanted);
    if (got == 0u) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }

    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(&eng->dec, input, (int)got,
                                      s_scratch_pcm, &info);
    if (info.frame_bytes > 0) {
        eng->file_pos += (size_t)info.frame_bytes;
    } else {
        eng->file_pos += 1u;
        return 0;
    }
    if (samples == 0) return 0;

    if (eng->sample_rate == 0u && info.hz > 0) {
        eng->sample_rate = (uint32_t)info.hz;
        eng->channels = info.channels;
        ESP_LOGI(TAG, "MP3 cache D%u: %d Hz, %d ch, %d kbps",
                 (unsigned)deck + 1u, info.hz, info.channels,
                 info.bitrate_kbps);
    }
#else
    if (!eng->fp || atomic_load_bool(&eng->eof)) return 0;
    uint8_t input[4096];
    long pos_before = ftell(eng->fp);
    size_t got = fread(input, 1u, sizeof(input), eng->fp);
    if (got == 0u) {
        atomic_store_bool(&eng->eof, true);
        return 0;
    }
    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(&eng->dec, input, (int)got,
                                      s_scratch_pcm, &info);
    if (info.frame_bytes > 0) {
        fseek(eng->fp, pos_before + (long)info.frame_bytes, SEEK_SET);
    } else {
        fseek(eng->fp, pos_before + 1L, SEEK_SET);
        return 0;
    }
    if (samples == 0) return 0;
    if (eng->sample_rate == 0u && info.hz > 0) {
        eng->sample_rate = (uint32_t)info.hz;
        eng->channels = info.channels;
        ESP_LOGI(TAG, "MP3: %d Hz, %d ch, %d kbps",
                 info.hz, info.channels, info.bitrate_kbps);
    }
#endif

    if (info.channels == 1) {
        for (int i = samples - 1; i >= 0; --i) {
            out_pcm[i * 2 + 1] = s_scratch_pcm[i];
            out_pcm[i * 2] = s_scratch_pcm[i];
        }
    } else {
        memcpy(out_pcm, s_scratch_pcm,
               (size_t)(samples * 2) * sizeof(int16_t));
    }
    return samples;
}

#if AE_FW
/*
 * seek_pvbr — fast O(1) seek using the 400-entry PVBR table.
 * Caller holds s_file_mutex.
 */
static void seek_pvbr(audio_engine_state_t *eng, uint32_t position_ms)
{
    if (eng->duration_ms > 0u && position_ms > eng->duration_ms) {
        position_ms = eng->duration_ms;
    }
    uint32_t idx = (eng->duration_ms > 0u)
                   ? (uint32_t)(((uint64_t)position_ms *
                                 (uint64_t)AUDIO_PVBR_LEN) /
                                (uint64_t)eng->duration_ms)
                   : 0u;
    if (idx >= AUDIO_PVBR_LEN) idx = AUDIO_PVBR_LEN - 1u;
    uint32_t target_byte = eng->pvbr[idx];

    if (target_byte > eng->file_size) target_byte = (uint32_t)eng->file_size;
    eng->file_pos = target_byte;
    ESP_LOGI(TAG, "PVBR seek %u ms → table[%u] = byte %u",
             (unsigned)position_ms, (unsigned)idx, (unsigned)target_byte);
}

/*
 * seek_estimate — O(1) seek used when neither an IFI seek-table nor a usable
 * PVBR table is available. Estimates the byte offset assuming roughly constant
 * bitrate, sets the read cursor there, and lets minimp3 resync to the next frame
 * header. This replaces the old linear decode-scan from the file start, which
 * ran a tight non-yielding loop (starving CPU 0 → task watchdog + UI freeze) and
 * could spin forever when the target was beyond the bytes streamed in so far.
 * A cache miss after the cursor move reloads the aligned page on demand.
 * Caller holds s_file_mutex.
 */
static void seek_estimate(audio_engine_state_t *eng, uint32_t position_ms)
{
    uint32_t target_byte = (eng->duration_ms > 0 && eng->file_size > 0)
        ? (uint32_t)(((uint64_t)position_ms * (uint64_t)eng->file_size) / eng->duration_ms)
        : 0u;
    if (target_byte > eng->file_size) target_byte = eng->file_size;

    eng->file_pos = target_byte;
    mp3dec_init(&eng->dec);
    atomic_store_bool(&eng->eof, false);
    ESP_LOGI(TAG, "Estimate seek %u ms → byte %u/%u (no PVBR/index)",
             (unsigned)position_ms, (unsigned)target_byte, (unsigned)eng->file_size);
}
#endif /* AE_FW */



/* ── Firmware decode + I2S output tasks (ESP32-P4) ────────────────────────── */
#if AE_FW
/* Per-deck decode scratch stays static and independent of the bounded compressed cache. */
static int16_t s_decode_pcm[AUDIO_ENGINE_DECK_COUNT][MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

#define AE_DIAG_OUTPUT_REPORT_BLOCKS 300u
#define AE_DIAG_DECODE_REPORT_FRAMES 120u
#define AE_DIAG_PRELOAD_REPORT_CHUNKS 64u

static audio_diag_counter_t s_diag_output_blocks;
static audio_diag_late_counter_t s_diag_output_late;

/* Per-phase worst-case timing for one output block. Owned by the output task
 * (write) and read without a lock for diagnostics; a torn read of a single
 * uint32 is not worth a mutex on the audio path. See the header for why the
 * gap phase is the interesting one. */
typedef struct {
    uint32_t mix_max_us;
    uint32_t push_max_us;
    uint32_t monitor_max_us;
    uint32_t main_max_us;
    uint32_t codec_max_us;
    uint32_t book_max_us;
    uint32_t head_max_us;
} ae_output_phase_stats_t;

static ae_output_phase_stats_t s_phase;

void audio_engine_reset_output_phase_stats(void)
{
    s_phase = (ae_output_phase_stats_t){ 0 };
}

/* Phase identity, so one block's timings can be compared against each other and
 * the worst one named. The running maxima answer "how bad does it get"; this
 * answers "which part was slow in the block that actually blew", which is the
 * open question behind the unexplained 370 ms outlier of 2026-07-21. */
typedef enum {
    AE_PH_HEAD = 0, AE_PH_MIX, AE_PH_PUSH, AE_PH_MONITOR,
    AE_PH_MAIN, AE_PH_CODEC, AE_PH_BOOK, AE_PH_COUNT
} ae_phase_id_t;

static const char *const AE_PHASE_NAME[AE_PH_COUNT] = {
    "head", "mix", "push", "monitor", "main", "codec", "book"
};

static uint32_t *const AE_PHASE_MAX_SLOT[AE_PH_COUNT] = {
    &s_phase.head_max_us, &s_phase.mix_max_us, &s_phase.push_max_us,
    &s_phase.monitor_max_us, &s_phase.main_max_us, &s_phase.codec_max_us,
    &s_phase.book_max_us,
};

/* Timings of the block currently being rendered, overwritten every block. */
static uint32_t s_phase_block[AE_PH_COUNT];

/* Coarse split of the mixer loop, so a mix-phase stall can be told apart:
 * confined to one group = a single expensive call, spread across all of them =
 * the whole loop running slow. */
#define AE_MIX_GROUPS 16
static uint32_t s_mix_group_max_us;
static uint32_t s_mix_group_worst;

static inline void ae_phase_note(ae_phase_id_t id, int64_t elapsed_us)
{
    uint32_t v = elapsed_us > 0 ? (uint32_t)elapsed_us : 0u;
    s_phase_block[id] = v;
    if (v > *AE_PHASE_MAX_SLOT[id]) {
        *AE_PHASE_MAX_SLOT[id] = v;
    }
}

/* Well above the 2x-period late warning (~11.6 ms at 44.1 kHz), so this only
 * fires on a genuine stall rather than ordinary jitter. Rate-limited because a
 * storm of them would flood the journal queue from the audio task. */
#define AE_BLOCK_OUTLIER_US     50000u
#define AE_OUTLIER_MIN_GAP_US   2000000

#if !defined(AUDIO_ENGINE_PC_TEST)
static int64_t s_last_outlier_us;

static void ae_report_block_outlier(uint32_t block_us)
{
    if (block_us < AE_BLOCK_OUTLIER_US) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (s_last_outlier_us != 0 && (now - s_last_outlier_us) < AE_OUTLIER_MIN_GAP_US) {
        return;
    }
    s_last_outlier_us = now;

    ae_phase_id_t worst = AE_PH_HEAD;
    for (int i = 1; i < AE_PH_COUNT; i++) {
        if (s_phase_block[i] > s_phase_block[worst]) {
            worst = (ae_phase_id_t)i;
        }
    }
    /* Total, the worst phase and its cost, plus the two normally dominant
     * phases for context. If the worst phase is one that does no real work
     * (head, codec), the block was preempted rather than slow. */
    /* For a mix-phase stall the group breakdown is the informative part: which
     * sixteenth of the loop ate the time, and how much of it. */
    service_log_event(SERVICE_LOG_AUDIO_BLOCK_OUTLIER, SERVICE_LOG_WARN,
                      4u, block_us, s_phase_block[worst],
                      s_mix_group_worst, s_mix_group_max_us,
                      AE_PHASE_NAME[worst]);
}
#endif
static audio_diag_counter_t s_diag_decode_frames[AUDIO_ENGINE_DECK_COUNT];
static audio_diag_counter_t s_diag_preload_chunks[AUDIO_ENGINE_DECK_COUNT];

static esp_err_t audio_output_service_open_codec(uint32_t sample_rate);
static esp_err_t audio_output_service_ensure_started(void);
static esp_err_t audio_output_service_stop(void);

static void ae_diag_reset(void)
{
    audio_diag_counter_init(&s_diag_output_blocks, AE_DIAG_OUTPUT_REPORT_BLOCKS);
    audio_diag_late_counter_init(&s_diag_output_late, 1u);
    s_phase = (ae_output_phase_stats_t){ 0 };
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
        atomic_store_bool(&eng->playing, false);
        atomic_store_bool(&eng->paused, false);
        atomic_store_bool(&eng->playback_finished, false);
    }
#if !defined(AUDIO_ENGINE_PC_TEST)
    /* Every audio load failure funnels through here, so this is the one place
     * that can tell the journal why a deck went to ERROR. Without it the deck
     * shows ERROR and the log says nothing — which is exactly the gap left once
     * a missing-analysis track is allowed through to the audio stage. */
    if (eng >= s_engines && eng < s_engines + AUDIO_ENGINE_DECK_COUNT) {
        service_log_event(SERVICE_LOG_AUDIO_LOAD_FAILED, SERVICE_LOG_ERROR,
                          2u, (uint32_t)(eng - s_engines) + 1u, (uint32_t)err,
                          0u, 0u, err_text ? err_text : "LOAD ERR");
    } else {
        service_log_event(SERVICE_LOG_AUDIO_LOAD_FAILED, SERVICE_LOG_ERROR,
                          1u, (uint32_t)err, 0u, 0u, 0u,
                          err_text ? err_text : "LOAD ERR");
    }
#endif
    audio_fw_preload_abort_load(fw, runtime);
}

/* The per-deck exit semaphore for a task's context. ctx->deck is bound before
 * the task is created, so it is always valid here; clamp defensively. */
static SemaphoreHandle_t ctx_tasks_done(const audio_fw_task_context_t *ctx)
{
    uint8_t d = (ctx && ctx->deck < AUDIO_ENGINE_DECK_COUNT) ? ctx->deck : 0u;
    return s_tasks_done[d];
}

static size_t ae_fw_cache_read_at(void *ctx, size_t offset,
                                  void *dst, size_t bytes)
{
    audio_fw_preload_t *fw = (audio_fw_preload_t *)ctx;
    FILE *src = fw ? (FILE *)fw->source : NULL;
    if (!src || !dst || bytes == 0u || offset >= fw->file_size) return 0u;
    media_io_gate_begin();
    if (!media_io_gate_is_available() || fseek(src, (long)offset, SEEK_SET) != 0) {
        media_io_gate_end();
        return 0u;
    }
    int64_t started = esp_timer_get_time();
    size_t got = fread(dst, 1u, bytes, src);
    uint32_t elapsed = (uint32_t)(esp_timer_get_time() - started);
    media_io_gate_end();
    ae_diag_record_preload_chunk((uint8_t)(fw - s_fw_preloads), elapsed,
                                 got, offset + got, fw->file_size);
    return got;
}

/* Loader opens the source and publishes a fixed-size seekable compressed cache.
 * After setup the decoder is the sole cache client; every backend miss is a
 * bounded READ under media_io_gate, while the PCM timeline absorbs I/O latency. */
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
        ae_fail_load(eng, fw, runtime, ESP_ERR_NOT_FOUND, "NOT FOUND");
        goto park;
    }
    fseek(src, 0, SEEK_END);
    long fsz = ftell(src);
    fseek(src, 0, SEEK_SET);
    media_io_gate_end();
    if (fsz <= 0) {
        media_io_gate_begin();
        fclose(src);
        media_io_gate_end();
        ae_fail_load(eng, fw, runtime, ESP_ERR_INVALID_SIZE, "BAD SIZE");
        goto park;
    }

    uint8_t *storage = heap_caps_malloc(AUDIO_FW_CACHE_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!storage) {
        media_io_gate_begin();
        fclose(src);
        media_io_gate_end();
        ae_fail_load(eng, fw, runtime, ESP_ERR_NO_MEM, "CACHE OOM");
        goto park;
    }
    if (!audio_fw_preload_bind_cache(fw, storage, AUDIO_FW_CACHE_BYTES,
                                     (size_t)fsz, src, ae_fw_cache_read_at)) {
        heap_caps_free(storage);
        media_io_gate_begin();
        fclose(src);
        media_io_gate_end();
        ae_fail_load(eng, fw, runtime, ESP_FAIL, "CACHE INIT ERR");
        goto park;
    }

    AE_LOCK();
    eng->file_size = (size_t)fsz;
    eng->file_pos = 0u;
    eng->fp = NULL;
    AE_UNLOCK();

    if (!audio_compressed_cache_prefetch(&fw->cache, 0u)) {
        ae_fail_load(eng, fw, runtime, ESP_FAIL, "CACHE READ ERR");
        goto park;
    }
    fw->loaded_bytes = fw->cache.backend_bytes;
    fw->load_done = true;
    eng->load_progress = 100u;
    ESP_LOGI(TAG, "bounded compressed cache D%u: file=%u cache=%u page=%u x %u",
             (unsigned)ctx->deck + 1u, (unsigned)fw->file_size,
             (unsigned)fw->buf_size, (unsigned)AUDIO_FW_CACHE_PAGE_BYTES,
             (unsigned)AUDIO_FW_CACHE_PAGE_COUNT);

park:
    while (runtime->run) vTaskDelay(pdMS_TO_TICKS(20));   /* stay alive until stop() */
    runtime->loader_task = NULL;
    xSemaphoreGive(ctx_tasks_done(ctx));
    vTaskDelete(NULL);
}

/* Decoder reads compressed data through the bounded cache. Cache misses are
 * serialized by media_io_gate; decoded PCM runway keeps the output task isolated. */
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

    /* Wait until the loader has opened the file and primed the bounded cache. */
    while (runtime->run && !fw->load_done) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!runtime->run) goto cleanup;

    if (eng->format == AUDIO_FORMAT_WAV || eng->format == AUDIO_FORMAT_FLAC) {
        const bool is_wav = (eng->format == AUDIO_FORMAT_WAV);
        AE_LOCK();
        esp_err_t init_rc = is_wav ? ae_wav_init_from_cache(eng, fw)
                                   : ae_flac_init_from_cache(eng, fw);
        AE_UNLOCK();
        if (init_rc != ESP_OK) {
            ESP_LOGE(TAG, "%s parse failed: %d", is_wav ? "WAV" : "FLAC", (int)init_rc);
            ae_fail_load(eng, fw, runtime, init_rc, is_wav ? "WAV ERR" : "FLAC ERR");
            goto cleanup;
        }
    } else {
        /* Latch the sample rate from the first decodable frame. A large ID3 tag
         * is handled by successive cache pages without allocating the full file. */
        int attempts = 0;
        while (runtime->run && eng->sample_rate == 0 && attempts < 256 &&
               !atomic_load_bool(&eng->eof)) {
            AE_LOCK();
            int64_t decode_start_us = esp_timer_get_time();
            int n = decode_one_frame(eng, fw, ctx->deck, decode_pcm);
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
             "track format D%u: %u Hz %d ch file=%u bytes cache_read=%u ready=%u",
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
                    !atomic_load_bool(&eng->playing) &&
                    eng->sample_rate > 0u && target_ms > 0u;
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
                atomic_store_bool(&eng->eof, false);
                atomic_store_bool(&eng->playback_finished, false);
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
                }
                if (seek_reason == AE_SEEK_REASON_SCRATCH_ABORT) {
                    atomic_store_bool(&s_scratch_abort_seek_waiting[ctx->deck], false);
                }
                eng->seek_reason    = AE_SEEK_REASON_USER;
                eng->seek_requested = false;
            }
            AE_UNLOCK();
        }


        /* Canonical mode freezes the producer for the complete scratch gesture;
         * the already-decoded future remains readable and immutable. */
        if (timeline_active(ctx->deck) &&
            (atomic_load_bool(&s_scratch_capture_freeze[ctx->deck]) ||
             atomic_load_bool(&s_scratch_playing[ctx->deck]))) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (atomic_load_bool(&eng->eof) ||
            deck_pcm_free(ctx->deck, eng->sample_rate) <
                (uint32_t)MINIMP3_MAX_SAMPLES_PER_FRAME) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        uint32_t scratch_newest_ms = 0u;
        bool scratch_newest_valid = false;
        AE_LOCK();
        int64_t decode_start_us = esp_timer_get_time();
        int  samples = decode_one_frame(eng, fw, ctx->deck, decode_pcm);
        uint32_t decode_us = (uint32_t)(esp_timer_get_time() - decode_start_us);
        /* How much of this batch may be published. Only a loop wrap lowers it,
         * and it is kept separate from `samples` on purpose: `samples <= 0` is
         * the decoder's own end-of-input signal further down, and borrowing it
         * to mean "publish nothing" would divert a mid-file loop wrap into the
         * EOF wait, which only the pending seek could release. */
        int publish_frames = samples;
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
                    /* Everything past loop_end has to go, or the loop plays it.
                     * The decoder runs ~2 s ahead of the playhead (measured), so
                     * arming a loop whose out point is at or behind the playhead
                     * leaves that whole lead already published and audible before
                     * the first pass — 1.96 s, about four beats, landing off the
                     * grid at most tempos. Trimming here also keeps the ring's
                     * wrap point exactly on loop_end, which is where the output
                     * task's time-based bookkeeping already assumes it is.
                     *
                     * `samples` has not been published yet (the push loop runs
                     * after this block), so the trim splits in two: withdraw what
                     * previous iterations already pushed past loop_end, and clamp
                     * this batch to whatever is still inside the loop. */
                    uint64_t keep_frames = 0u;
                    if (eng->loop_end_ms > eng->seek_base_ms) {
                        keep_frames = ((uint64_t)(eng->loop_end_ms - eng->seek_base_ms) *
                                       (uint64_t)eng->sample_rate) / 1000u;
                    }
                    uint64_t published = eng->frames_since_seek - (uint64_t)samples;
                    if (published > keep_frames) {
                        uint32_t excess = (uint32_t)(published - keep_frames);
                        /* Leave the output something to play while the decoder
                         * reseeks and refills. Withdrawing the entire runway is
                         * right in principle and wrong in practice: measured on
                         * hardware, arming a manual loop withdrew 85802 frames
                         * (1.95 s) and the ring then ran dry for 512 frames -
                         * 11.6 ms of silence, heard as a click. The frames kept
                         * back are past loop_end, so this trades at most ~46 ms
                         * of overrun for no dropout, against the 1946 ms of
                         * overrun it replaced. */
                        uint32_t runway = deck_pcm_used(ctx->deck);
                        uint32_t max_drop = runway > AE_LOOP_TRIM_MIN_RUNWAY_FRAMES
                            ? runway - AE_LOOP_TRIM_MIN_RUNWAY_FRAMES
                            : 0u;
                        if (excess > max_drop) excess = max_drop;
                        /* The output task pops without AE_LOCK; shut out
                         * preemption while write_seq moves down, exactly as the
                         * user-seek ring flush below does. */
                        taskENTER_CRITICAL(&s_ring_flush_mux);
                        uint32_t dropped = deck_pcm_drop_newest(ctx->deck, excess);
                        taskEXIT_CRITICAL(&s_ring_flush_mux);
                        s_loop_trim_dropped_total[ctx->deck] += dropped;
                        if (dropped > s_loop_trim_dropped_max[ctx->deck]) {
                            s_loop_trim_dropped_max[ctx->deck] = dropped;
                        }
                        s_loop_trim_clamped_total[ctx->deck] += (uint32_t)publish_frames;
                        publish_frames = 0;  /* none of this batch is in the loop */
                    } else {
                        uint64_t room = keep_frames - published;
                        if ((uint64_t)publish_frames > room) {
                            s_loop_trim_clamped_total[ctx->deck] +=
                                (uint32_t)(publish_frames - (int)room);
                            publish_frames = (int)room;
                        }
                    }
                    s_loop_trim_wraps[ctx->deck]++;
                    eng->seek_target_ms = eng->loop_start_ms;
                    eng->seek_reason    = AE_SEEK_REASON_LOOP; /* gapless ring */
                    eng->seek_requested = true;
                }
            }
        }
        bool eof = atomic_load_bool(&eng->eof);
        size_t file_pos = eng->file_pos;
        AE_UNLOCK();

        if (eof && samples <= 0) {
            /* Decoder EOF is not transport EOF. The output task remains active
             * until it drains all already-decoded future PCM. */
            while (atomic_load_bool(&eng->eof) && runtime->run) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
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

        /* Freeze scratch capture while this deck is scratching so the window's
         * newest frame stays put under the jog-driven read head (the head is
         * measured as frames-back-from-newest). Ring capture continues so normal
         * playback can resume on release (which seeks + flushes anyway). */
        bool capture_scratch = timeline_active(ctx->deck) &&
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
        bool capture_interrupted = false;
        for (int i = 0; i < publish_frames && runtime->run; i++) {
            /* scratch_begin may freeze the canonical producer while this
             * decoded batch is being published. Stop at the next frame so the
             * writer flag can be released promptly; scratch release performs
             * an authoritative seek, so discarding the unpublished tail cannot
             * create a resumed-playback discontinuity. */
            if (capture_scratch && timeline_active(ctx->deck) &&
                (atomic_load_bool(&s_scratch_capture_freeze[ctx->deck]) ||
                 atomic_load_bool(&s_scratch_playing[ctx->deck]))) {
                capture_interrupted = true;
                break;
            }
            while (deck_pcm_free(ctx->deck, eng->sample_rate) == 0u && runtime->run) {
                if (capture_scratch && timeline_active(ctx->deck) &&
                    (atomic_load_bool(&s_scratch_capture_freeze[ctx->deck]) ||
                     atomic_load_bool(&s_scratch_playing[ctx->deck]))) {
                    capture_interrupted = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            if (capture_interrupted) break;
            (void)deck_pcm_push(ctx->deck, decode_pcm[i * 2], decode_pcm[i * 2 + 1]);
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
        if (timeline_active(ctx->deck) && scratch_newest_valid && !capture_interrupted) {
            sync_scratch_view_from_timeline(ctx->deck, scratch_newest_ms);
        }
        if (capture_scratch) {
            atomic_store_bool(&s_scratch_capture_writing[ctx->deck], false);
        }
    }

cleanup:
    /* The preload buffer / file are owned by the loader + deck stop path. */
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
        float effective_pitch = engine_pitch_load(deck) * (1.0f + jog_bend_load(deck));
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
        audio_filter_set_sample_rate(&s_deck_filter[deck], sample_rate);
        audio_filter_set_sample_rate(&s_beat_fx_filter[deck], sample_rate);
        audio_filter_set_sample_rate(&s_pad_fx[deck].filter, sample_rate);
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
    uint32_t consecutive_busy_blocks = 0u;
    while (s_output_run) {
        if (!s_output_codec_open) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        for (uint8_t d = 0u; d < AUDIO_ENGINE_DECK_COUNT; d++) {
            complete_eof_drain_if_ready(d);
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

        audio_output_apply_master_tempo_commands();
        audio_output_apply_pending_fx_commands();

        float deck0_gain = 1.0f;
        float deck1_gain = 1.0f;
        audio_engine_get_output_gains(&deck0_gain, &deck1_gain);
        /* Pre-fader gain (pregain x master trim) for the VU meters, so they track
         * the TRIM knob independent of the channel fader/crossfader. */
        float deck0_prefader_gain =
            pregain_gain_from_raw(atomic_load_u16(&s_pregain[0])) * master_trim_load();
        float deck1_prefader_gain =
            pregain_gain_from_raw(atomic_load_u16(&s_pregain[1])) * master_trim_load();
        bool smart_cfx_enabled = atomic_load_bool(&s_smart_cfx_enabled);
        bool pfl0_enabled = atomic_load_bool(&s_pfl_enabled[AE_DECK_0]);
        bool pfl1_enabled = atomic_load_bool(&s_pfl_enabled[1u]);
        bool master_cue_enabled = atomic_load_bool(&s_master_cue_enabled);
        uint16_t headphone_mix = atomic_load_u16(&s_headphone_mix);
        uint16_t headphone_level = atomic_load_u16(&s_headphone_level);
        audio_output_mixer_controls_t mixer_controls;
        audio_output_mixer_prepare_controls(&mixer_controls,
                                             pfl0_enabled,
                                             pfl1_enabled,
                                             output_headphone_mode(),
                                             headphone_mix,
                                             headphone_level,
                                             master_cue_enabled);

        const uint8_t deck0_index = AE_DECK_0;
        const uint8_t deck1_index = 1u;
        const float deck0_pitch = engine_pitch_load(deck0_index) *
                                  (1.0f + jog_bend_load(deck0_index));
        const float deck1_pitch = engine_pitch_load(deck1_index) *
                                  (1.0f + jog_bend_load(deck1_index));
        audio_output_mixer_deck_t deck0 = {
            .active = deck_output_active(deck0_index),
            .pitch_factor = deck0_pitch,
            .resample_factor = audio_output_mixer_resample_factor(
                deck0_pitch,
                s_engines[deck0_index].sample_rate, s_output_sample_rate),
            .keylock_rate_ratio = audio_output_mixer_rate_ratio(
                s_engines[deck0_index].sample_rate, s_output_sample_rate),
            .source_sample_rate = s_engines[deck0_index].sample_rate,
            .output_sample_rate = s_output_sample_rate,
            .gain = deck0_gain,
            .eq = &s_deck_eq[deck0_index],
            .filter = &s_deck_filter[deck0_index],
            .filter_enabled = smart_cfx_enabled,
            .beat_fx_filter = &s_beat_fx_filter[deck0_index],
            .beat_fx_filter_enabled =
                (s_beat_fx_filter_applied[deck0_index] & AE_FILTER_CMD_ENABLED) != 0u,
            .beat_fx_flanger = &s_beat_fx_flanger[deck0_index],
            .beat_fx_flanger_enabled = s_beat_fx_flanger[deck0_index].config.enabled,
            .beat_fx_echo = &s_beat_fx_echo[deck0_index],
            .beat_fx_echo_enabled = s_beat_fx_echo[deck0_index].config.enabled,
            .pad_fx = &s_pad_fx[deck0_index],
            .resampler = resampler_for_deck(deck0_index),
            .pop_source = pop_deck_source,
            .source_ctx = &s_scratch_ctx_deck[deck0_index],
            .keylock_active = atomic_load_bool(&s_master_tempo_enabled[deck0_index]) &&
                              timeline_active(deck0_index),
            .keylock_render = ae_keylock_render_cb,
            .keylock_ctx = &s_scratch_ctx_deck[deck0_index],
            .scratch_active = atomic_load_bool(&s_scratch_playing[deck0_index]),
            .scratch_render = ae_scratch_render_cb,
            .scratch_ctx = &s_scratch_ctx_deck[deck0_index],
        };
        audio_output_mixer_deck_t deck1 = {
            .active = deck_output_active(deck1_index),
            .pitch_factor = deck1_pitch,
            .resample_factor = audio_output_mixer_resample_factor(
                deck1_pitch,
                s_engines[deck1_index].sample_rate, s_output_sample_rate),
            .keylock_rate_ratio = audio_output_mixer_rate_ratio(
                s_engines[deck1_index].sample_rate, s_output_sample_rate),
            .source_sample_rate = s_engines[deck1_index].sample_rate,
            .output_sample_rate = s_output_sample_rate,
            .gain = deck1_gain,
            .eq = &s_deck_eq[deck1_index],
            .filter = &s_deck_filter[deck1_index],
            .filter_enabled = smart_cfx_enabled,
            .beat_fx_filter = &s_beat_fx_filter[deck1_index],
            .beat_fx_filter_enabled =
                (s_beat_fx_filter_applied[deck1_index] & AE_FILTER_CMD_ENABLED) != 0u,
            .beat_fx_flanger = &s_beat_fx_flanger[deck1_index],
            .beat_fx_flanger_enabled = s_beat_fx_flanger[deck1_index].config.enabled,
            .beat_fx_echo = &s_beat_fx_echo[deck1_index],
            .beat_fx_echo_enabled = s_beat_fx_echo[deck1_index].config.enabled,
            .pad_fx = &s_pad_fx[deck1_index],
            .resampler = resampler_for_deck(deck1_index),
            .pop_source = pop_deck_source,
            .source_ctx = &s_scratch_ctx_deck[deck1_index],
            .keylock_active = atomic_load_bool(&s_master_tempo_enabled[deck1_index]) &&
                              timeline_active(deck1_index),
            .keylock_render = ae_keylock_render_cb,
            .keylock_ctx = &s_scratch_ctx_deck[deck1_index],
            .scratch_active = atomic_load_bool(&s_scratch_playing[deck1_index]),
            .scratch_render = ae_scratch_render_cb,
            .scratch_ctx = &s_scratch_ctx_deck[deck1_index],
        };

        /* Decay the jog nudge once per output block; snap tiny residuals to 0. */
        for (uint8_t d = 0; d < AUDIO_ENGINE_DECK_COUNT; d++) {
            jog_bend_decay(d);
        }

        if (!deck0.active && !deck1.active) {
            /* No audio block will reach the normal peak-recording path below,
             * but the UI meter still needs zero-input release ticks. */
            decay_idle_deck_ui_peaks();
#if !defined(AUDIO_ENGINE_PC_TEST) && CONFIG_AUDIO_RECORDER_ENABLED
            /* Keep the recording timeline continuous across an idle gap by
             * pushing correctly paced silence at the established output rate. */
            if (audio_recorder_get_state() == AUDIO_RECORDER_RECORDING) {
                memset(master_out, 0, AE_OUT_FRAMES * 2 * sizeof(int16_t));
                audio_recorder_push_master(master_out, AE_OUT_FRAMES, s_output_sample_rate);
            }
#endif
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t consumed[AUDIO_ENGINE_DECK_COUNT] = { 0 };
        s_scratch_handoff_consumed[0] = 0u;
        s_scratch_handoff_consumed[1] = 0u;
        uint16_t block_peak[AUDIO_ENGINE_DECK_COUNT] = { 0 };
        audio_mixer_limiter_stats_t block_limiter_stats = { 0 };

        int64_t phase_t0 = esp_timer_get_time();
        int64_t phase_mark = phase_t0;
        ae_phase_note(AE_PH_HEAD, phase_t0 - block_start_us);

        /* Split the mixer loop into a few coarse groups. A stall confined to one
         * group means a single expensive call; one spread evenly across all of
         * them means the whole loop is running slow, which points at memory or
         * cache rather than at any one operation. Sixteen timer reads per block
         * is a fraction of a percent of the ~5.8 ms period, unlike timing every
         * one of the 256 frames. */
        uint32_t mix_group_max_us = 0u;
        uint32_t mix_group_worst = 0u;
        int64_t mix_group_mark = esp_timer_get_time();
        const int mix_group_len = AE_OUT_FRAMES / AE_MIX_GROUPS;

        for (int i = 0; i < AE_OUT_FRAMES; i++) {
            uint32_t frame_consumed0 = 0;
            uint32_t frame_consumed1 = 0;

            audio_output_mix_result_t mix = audio_output_mixer_next_prepared(
                &deck0,
                &deck1,
                &mixer_controls,
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

            if (mix_group_len > 0 && ((i + 1) % mix_group_len) == 0) {
                int64_t now_g = esp_timer_get_time();
                int64_t took = now_g - mix_group_mark;
                mix_group_mark = now_g;
                if (took > 0 && (uint32_t)took > mix_group_max_us) {
                    mix_group_max_us = (uint32_t)took;
                    mix_group_worst = (uint32_t)((i + 1) / mix_group_len) - 1u;
                }
            }
        }
        s_mix_group_max_us = mix_group_max_us;
        s_mix_group_worst = mix_group_worst;
        consumed[deck0_index] += s_scratch_handoff_consumed[deck0_index];
        consumed[deck1_index] += s_scratch_handoff_consumed[deck1_index];
        {
            int64_t now = esp_timer_get_time();
            ae_phase_note(AE_PH_MIX, now - phase_mark);
            phase_mark = now;
        }
#if !defined(AUDIO_ENGINE_PC_TEST) && CONFIG_AUDIO_RECORDER_ENABLED
        /* Tap the exact post-limiter MAIN block for the optional recorder. This
         * is a no-op (single atomic load) unless recording is active. */
        audio_recorder_push_master(master_out, AE_OUT_FRAMES, s_output_sample_rate);
#endif
        {
            int64_t now = esp_timer_get_time();
            ae_phase_note(AE_PH_PUSH, now - phase_mark);
            phase_mark = now;
        }
        (void)monitor_pcm_link_write_nonblocking(hp_out, AE_OUT_FRAMES);
        {
            int64_t now = esp_timer_get_time();
            ae_phase_note(AE_PH_MONITOR, now - phase_mark);
            phase_mark = now;
        }
        esp_err_t main_rc = audio_output_write_main(master_out, AE_OUT_FRAMES * 2 * sizeof(int16_t));
        {
            int64_t now = esp_timer_get_time();
            ae_phase_note(AE_PH_MAIN, now - phase_mark);
            phase_mark = now;
        }
        /* When ES8311 is disabled the loop paces on the PCM5102A blocking
           write above; hp_out still reaches the FLX4 phones over the link. */
        esp_err_t hp_rc = ESP_ERR_NOT_SUPPORTED;
        if (s_codec) {
            hp_rc = esp_codec_dev_write(s_codec, hp_out, (int)(AE_OUT_FRAMES * 2 * sizeof(int16_t)));
        }

        {
            int64_t now = esp_timer_get_time();
            ae_phase_note(AE_PH_CODEC, now - phase_mark);
            phase_mark = now;
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
        ae_phase_note(AE_PH_BOOK, esp_timer_get_time() - phase_mark);
        int64_t block_elapsed_us = esp_timer_get_time() - block_start_us;
#if !defined(AUDIO_ENGINE_PC_TEST)
        ae_report_block_outlier(block_elapsed_us > 0 ? (uint32_t)block_elapsed_us : 0u);
#endif
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
        bool scratch_writer_needs_cpu = false;
        for (uint8_t d = 0u; d < AUDIO_ENGINE_DECK_COUNT; d++) {
            if (atomic_load_bool(&s_scratch_capture_freeze[d]) &&
                atomic_load_bool(&s_scratch_capture_writing[d])) {
                scratch_writer_needs_cpu = true;
                break;
            }
        }
        if (block_delay_ms > 0u) {
            vTaskDelay(pdMS_TO_TICKS(block_delay_ms));
            consecutive_busy_blocks = 0u;
        } else if (scratch_writer_needs_cpu ||
                   audio_output_should_force_idle(++consecutive_busy_blocks)) {
            /* taskYIELD only offers CPU0 to equal/higher-priority tasks. Give
             * the lower-priority decoder one real tick immediately when a
             * scratch freeze is waiting for its writer flag. The same delay
             * also gives IDLE0 one real tick periodically for its watchdog
             * during continuous DSP. */
            vTaskDelay(pdMS_TO_TICKS(1));
            consecutive_busy_blocks = 0u;
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
#if AE_FW
        atomic_store_bool(&s_start_waiting[deck], false);
#endif
    }
    memset(eng, 0, sizeof(*eng));
    engine_pitch_store((uint8_t)(eng - s_engines), 1.0f);
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
#if AE_FW
        atomic_store_bool(&s_start_waiting[i], false);
        s_start_wait_count[i] = 0u;
#endif
        s_loop_trim_wraps[i] = 0u;
        s_loop_trim_dropped_max[i] = 0u;
        s_loop_trim_dropped_total[i] = 0u;
        s_loop_trim_clamped_total[i] = 0u;
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
        uint32_t filter_command = pack_filter_command(AUDIO_FILTER_RAW_CENTER, false);
        __atomic_store_n(&s_beat_fx_filter_command[i], filter_command,
                         __ATOMIC_RELAXED);
        s_beat_fx_filter_applied[i] = filter_command;
    }
    init_beat_fx_echo_buffers();
    init_beat_fx_flanger_buffers();
    init_pad_fx_buffers();
    init_scratch_buffers();
    atomic_store_u16(&s_crossfader, AUDIO_MIXER_CONTROL_CENTER);
    master_trim_store(1.0f);
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
    audio_pcm_ring_t *ring = &s_pcm_rings[deck]; /* bounded playback fallback */
#endif
    eng->last_error = ESP_OK;
    snprintf(eng->last_error_text, sizeof(eng->last_error_text), "OK");

    if (!mp3_path) {
        eng->last_error = ESP_ERR_INVALID_ARG;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "INVALID ARG");
        return ESP_ERR_INVALID_ARG;
    }

    /* A failed asynchronous load can clear loaded while loader/decoder
     * tasks and the PSRAM preload buffer still belong to the old session. Always
     * stop and join the previous session before publishing run=true for a retry. */
    esp_err_t stop_rc = audio_engine_stop_for_deck(deck);
    if (stop_rc != ESP_OK) {
        eng->last_error = stop_rc;
        snprintf(eng->last_error_text, sizeof(eng->last_error_text), "STOP ERR");
        return stop_rc;
    }

    eng->loading = true;   /* cleared when the codec opens (FW) / at end (PC) */
    eng->load_progress = 0;

#if AE_FW
    audio_format_t detected_format = audio_format_detect_path(mp3_path);
    /* MP3, WAV and FLAC share a bounded seekable compressed-page cache;
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
    atomic_store_bool(&eng->playing, false);
    atomic_store_bool(&eng->paused, false);
    atomic_store_bool(&eng->eof, false);
    atomic_store_bool(&eng->playback_finished, false);
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
                                    AE_DECK_0,
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
        if (exited == runtime->tasks_started) {
            if (fw->source) {
                media_io_gate_begin();
                fclose((FILE *)fw->source);
                media_io_gate_end();
                fw->source = NULL;
            }
            if (fw->buf) {
                heap_caps_free(fw->buf);
                fw->buf = NULL;
            }
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
        if (exited == runtime->tasks_started) {
            if (fw->source) {
                media_io_gate_begin();
                fclose((FILE *)fw->source);
                media_io_gate_end();
                fw->source = NULL;
            }
            if (fw->buf) {
                heap_caps_free(fw->buf);
                fw->buf = NULL;
            }
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

static esp_err_t audio_engine_play_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded) return ESP_ERR_INVALID_STATE;

    /* A decoder can reach EOF before first PLAY because it fills the future PCM
     * window while the deck is paused. Rewind only after the consumer has
     * explicitly marked that decoded tail as fully played. */
    if (audio_eof_policy_play_requires_rewind(
            atomic_load_bool(&eng->playback_finished))) {
        esp_err_t seek_rc = audio_engine_seek_for_deck_reason(
            deck, 0u, AE_SEEK_REASON_USER);
        if (seek_rc != ESP_OK) return seek_rc;
#if AE_FW
        audio_resampler_reset(&s_resamplers[deck]);
        s_keylocks[deck].initialized = false;
#endif
    }

#if AE_FW
    bool wait_for_prebuffer =
        !audio_start_gate_ready(deck_pcm_used(deck),
                                AE_START_PREBUFFER_FRAMES,
                                atomic_load_bool(&eng->eof));
    atomic_store_bool(&s_start_waiting[deck], wait_for_prebuffer);
    if (wait_for_prebuffer) {
        __atomic_add_fetch(&s_start_wait_count[deck], 1u, __ATOMIC_RELAXED);
    }
#endif
    atomic_store_bool(&eng->paused, false);
    atomic_store_bool(&eng->playing, true);
    return ESP_OK;
}

static esp_err_t audio_engine_pause_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    if (!eng->loaded) return ESP_ERR_INVALID_STATE;

    atomic_store_bool(&eng->paused, true);
    atomic_store_bool(&eng->playing, false);
#if AE_FW
    atomic_store_bool(&s_start_waiting[deck], false);
#endif
    return ESP_OK;
}

/* ── audio_engine_stop ────────────────────────────────────────────────────── */
static esp_err_t audio_engine_stop_for_deck(uint8_t deck)
{
    audio_engine_state_t *eng = &s_engines[deck];
    /* Do not key teardown on eng->loaded: error paths deliberately clear that
     * flag before the parked tasks have exited. Runtime task ownership and the
     * preload pointer are the authoritative session-liveness indicators. */

    atomic_store_bool(&eng->playing, false);
    atomic_store_bool(&eng->paused, false);
    atomic_store_bool(&eng->playback_finished, false);
#if AE_FW
    atomic_store_bool(&s_start_waiting[deck], false);
#endif
    eng->loading = false;
    eng->load_progress = 100;

#if AE_FW
    audio_fw_runtime_t *runtime = &s_fw_runtimes[deck];
    if (runtime->run || runtime->tasks_started > 0) {
        runtime->run = false;
        atomic_store_bool(&eng->eof, false); /* wake decode task if parked at EOF */
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
    eng->file_size = 0;
    eng->file_pos  = 0;
    AE_UNLOCK();

#if AE_FW
    audio_fw_preload_t *fw = &s_fw_preloads[deck];
    if (fw->source) {
        media_io_gate_begin();
        fclose((FILE *)fw->source);
        media_io_gate_end();
        fw->source = NULL;
    }
    if (fw->buf) {
        heap_caps_free(fw->buf);
        fw->buf = NULL;
    }
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

/* ── audio_engine_seek ────────────────────────────────────────────────────── */
static esp_err_t audio_engine_seek_for_deck_reason(uint8_t deck,
                                                   uint32_t position_ms,
                                                   ae_seek_reason_t reason)
{
    audio_engine_state_t *eng = &s_engines[deck];
    #if AE_FW
    if (!eng->loaded || !s_fw_preloads[deck].source) return ESP_ERR_INVALID_STATE;
#else
    if (!eng->loaded || (!eng->fp && !eng->decoder_open)) return ESP_ERR_INVALID_STATE;
#endif

    AE_LOCK();
    /* The decode task writes the same seek fields under the lock (loop-wrap
     * seek and end-of-handling clear), so a user seek must publish them under
     * the lock too — otherwise it can be lost or downgraded to a no-flush
     * loop seek when the writes interleave. */
    eng->seek_target_ms = position_ms;
    eng->seek_reason    = (uint8_t)reason;
    eng->seek_requested = true;
    atomic_store_bool(&eng->eof, false); /* also wakes decode thread if at EOF */
    atomic_store_bool(&eng->playback_finished, false);
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
static void audio_engine_set_pitch_percent_for_deck(uint8_t deck, float percent)
{
    if (!isfinite(percent)) percent = 0.0f;
    float factor = 1.0f + (percent / 100.0f);
    /* Clamp to ±20% to stay sane even if fader value is out of range */
    if (factor < 0.80f) factor = 0.80f;
    if (factor > 1.20f) factor = 1.20f;
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        pending_pitch_store(deck, factor);
        atomic_store_bool(&s_pending_pitch_valid[deck], true);
        return;
    }
    engine_pitch_store(deck, factor);
}

static void audio_engine_set_pitch_for_deck(uint8_t deck, int16_t raw_pitch)
{
    audio_engine_set_pitch_percent_for_deck(deck, audio_engine_raw_pitch_to_percent(raw_pitch));
}

float audio_engine_raw_pitch_to_percent(int16_t raw_pitch)
{
    return ((8192.0f - (float)raw_pitch) / 8192.0f) * 10.0f;
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

static bool deck_is_valid(uint8_t deck);

static ae_state_t engine_lifecycle_state(const audio_engine_state_t *eng)
{
    if (!eng) return AE_IDLE;
    if (eng->last_error != ESP_OK) return AE_ERROR;
    if (!eng->loaded) return AE_IDLE;
    if (eng->loading) return AE_LOADING;
    return (atomic_load_bool(&eng->playing) &&
            !atomic_load_bool(&eng->paused)) ? AE_PLAYING : AE_READY;
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
    out->playing = atomic_load_bool(&eng->playing) &&
                   !atomic_load_bool(&eng->paused);
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
                                       AE_DECK_0,
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

void audio_engine_deck_set_master_tempo(uint8_t deck, bool enabled)
{
    if (!deck_is_valid(deck)) return;
#if AE_FW
    atomic_store_bool(&s_master_tempo_enabled[deck], enabled);
    (void)__atomic_add_fetch(&s_master_tempo_command_epoch[deck],
                             1u, __ATOMIC_RELEASE);
#else
    (void)enabled;
#endif
}

bool audio_engine_deck_master_tempo_enabled(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
#if AE_FW
    return atomic_load_bool(&s_master_tempo_enabled[deck]);
#else
    return false;
#endif
}

void audio_engine_deck_jog_nudge(uint8_t deck, int16_t delta)
{
    if (!deck_is_valid(deck) || delta == 0) return;
    jog_bend_add(deck, (float)delta * AE_JOG_BEND_PER_TICK);
}

void audio_engine_deck_set_hold(uint8_t deck, bool held)
{
    if (!deck_is_valid(deck)) return;
    /* Leaving hold cancels any leftover jog nudge so the deck resumes at exactly
     * the fader tempo, not with a stray bend from before the platter was grabbed. */
    if (!held) {
        jog_bend_store(deck, 0.0f);
    }
    atomic_store_bool(&s_deck_hold[deck], held);
}

bool audio_engine_deck_scratch_begin(uint8_t deck)
{
    if (!deck_is_valid(deck)) return false;
    if (!timeline_active(deck)) {
        ESP_LOGW(TAG,
                 "scratch begin D%u unavailable: canonical timeline not allocated -> platter hold",
                 (unsigned)deck);
        return false;
    }
    if (atomic_load_bool(&s_scratch_playing[deck])) {
        /* Idempotent active touch, or a fast re-grab before the release fade has
         * completed. The frozen canonical window and scratch head are still
         * valid: cancel the handoff and route the next block back to scratch. */
        ae_scratch_handoff_t phase =
            (ae_scratch_handoff_t)scratch_handoff_load(&s_scratch_handoff[deck]);
        s_scratch_handoff_gain[deck] = 1.0f;
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
    sync_scratch_view_from_timeline(deck, b->newest_pos_ms);
    uint32_t used = audio_scratch_buffer_used(b);
    if (!b->frames || !b->newest_valid || b->sample_rate == 0u || used < 2u) {
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        ESP_LOGW(TAG,
                 "scratch begin D%u rejected: window frames=%u newest=%u rate=%u used=%u",
                 (unsigned)deck, b->frames ? 1u : 0u, b->newest_valid ? 1u : 0u,
                 (unsigned)b->sample_rate, (unsigned)used);
        return false;
    }
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
    uint64_t back = (write_seq - 1u) - play_seq;
    if (back >= used) {
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        ESP_LOGW(TAG, "scratch begin D%u rejected: head back=%llu used=%u",
                 (unsigned)deck, (unsigned long long)back, (unsigned)used);
        return false;
    }
    bool started_paused = !atomic_load_bool(&eng->playing) ||
                          atomic_load_bool(&eng->paused);
    atomic_store_bool(&s_scratch_started_paused[deck], started_paused);
    atomic_store_bool(&s_scratch_return_paused[deck], false);
    s_scratch_origin_pos_ms[deck] = audio_engine_position_ms_for_deck(deck);
    s_scratch_origin_play_seq[deck] =
        (uint32_t)audio_pcm_timeline_play_seq(&s_pcm_timelines[deck]);
    float head_back = (float)back;
    float frames_per_tick = AUDIO_SCRATCH_DEFAULT_FRAMES_PER_TICK *
        ((float)b->sample_rate / (float)AE_TIMELINE_MAX_RATE);
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
        (void)audio_pcm_timeline_set_playhead(
            &s_pcm_timelines[deck], s_scratch_origin_play_seq[deck]);
        s_engines[deck].output_base_ms = s_scratch_origin_pos_ms[deck];
        s_engines[deck].output_frames_since_seek = 0u;
#if AE_FW
        audio_resampler_reset(&s_resamplers[deck]);
#endif
        atomic_store_bool(&s_scratch_return_paused[deck], true);
    }

    if (!return_paused && b->newest_valid && b->sample_rate > 0u) {
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
    return atomic_load_bool(&s_engines[deck].playing) &&
           !atomic_load_bool(&s_engines[deck].paused);
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
        atomic_store_u32(&s_beat_fx_echo_mode[deck], AUDIO_DELAY_FX_MODE_ECHO);
        s_beat_fx_echo_command[deck] = (ae_fx_command_t) { 0 };
        s_beat_fx_echo_applied[deck] = publish_echo_command(
            deck, &(audio_delay_fx_config_t) {
                .mode = AUDIO_DELAY_FX_MODE_ECHO,
            });
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
        s_beat_fx_flanger_command[deck] = (ae_fx_command_t) { 0 };
        s_beat_fx_flanger_applied[deck] = publish_flanger_command(
            deck, &(audio_flanger_fx_config_t) { 0 });
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
        uint32_t command = pack_pad_fx_command((audio_pad_fx_config_t) {
            .mode = AUDIO_PAD_FX_MODE_PAD_FX1,
            .pad = 0u,
            .active = false,
        });
        __atomic_store_n(&s_pad_fx_command[deck], command, __ATOMIC_RELAXED);
        s_pad_fx_applied[deck] = command;
    }
}

/* Allocate the per-deck canonical PCM stores (once) and bind each scratch view.
 * Stereo, so capacity*2 int16. On PSRAM (~768 KB/deck at 4 s @ 48 kHz). If an
 * allocation fails, the deck keeps normal ring playback but audible scratch is
 * unavailable and deck_core safely falls back to platter-hold. */
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
        audio_scratch_buffer_init(&s_scratch_buf[deck],
                                  s_pcm_timeline_storage[deck],
                                  s_pcm_timeline_storage[deck]
                                      ? AE_TIMELINE_CAPACITY_FRAMES : 0u);
        ESP_LOGI(TAG, "PCM timeline D%u: %s, capacity=%u frames",
                 (unsigned)deck,
                 timeline_active(deck) ? "PSRAM canonical" : "ring playback; scratch unavailable",
                 (unsigned)(timeline_active(deck)
                     ? AE_TIMELINE_CAPACITY_FRAMES : AUDIO_PCM_RING_FRAMES));
        audio_scratch_init(&s_scratch_engine[deck]);
        atomic_store_bool(&s_scratch_playing[deck], false);
        atomic_store_bool(&s_scratch_capture_freeze[deck], false);
        atomic_store_bool(&s_scratch_capture_writing[deck], false);
        atomic_store_bool(&s_scratch_abort_seek_requested[deck], false);
        atomic_store_bool(&s_scratch_abort_seek_waiting[deck], false);
        atomic_store_bool(&s_scratch_regrab_requested[deck], false);
        atomic_store_bool(&s_scratch_started_paused[deck], false);
        atomic_store_bool(&s_scratch_return_paused[deck], false);
        atomic_store_bool(&s_pending_pitch_valid[deck], false);
        pending_pitch_store(deck, 1.0f);
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
bool audio_engine_test_snapshot_beat_fx_time_command(
    uint8_t deck,
    audio_delay_fx_config_t *out_config)
{
    if (!deck_is_valid(deck) || !out_config) return false;
    uint32_t sequence = 0u;
    return snapshot_echo_command(deck, &sequence, out_config);
}

void audio_engine_test_record_deck_peak(uint8_t deck, int16_t left, int16_t right)
{
    AE_LOCK();
    audio_mixer_frame_t frame = { .left = left, .right = right };
    record_deck_peak(deck, frame);
    record_deck_ui_peak(deck, frame_peak(frame));
    AE_UNLOCK();
}

void audio_engine_test_decay_idle_deck_peaks(void)
{
    decay_idle_deck_ui_peaks();
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

void audio_engine_test_disable_pcm_timeline(uint8_t deck)
{
    if (!deck_is_valid(deck)) return;
    /* Retain the allocated pointer so a later audio_engine_init() can restore
     * the normal test backend without leaking or reallocating host memory. */
    audio_pcm_timeline_init(&s_pcm_timelines[deck], NULL, 0u);
    audio_scratch_buffer_init(&s_scratch_buf[deck], NULL, 0u);
    audio_pcm_ring_reset(&s_pcm_rings[deck]);
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
        uint32_t command = pack_filter_command(
            deck_enabled ? raw : AUDIO_FILTER_RAW_CENTER, deck_enabled);
        __atomic_store_n(&s_beat_fx_filter_command[deck], command,
                         __ATOMIC_RELEASE);
    }
    return ESP_OK;
}

static uint16_t beat_fx_time_wet_from_depth(uint8_t depth)
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

static esp_err_t audio_engine_set_beat_fx_time_effect(
    audio_engine_beat_fx_target_t target,
    uint8_t depth,
    uint32_t delay_ms,
    audio_delay_fx_mode_t requested_mode,
    bool enabled)
{
    if (target != AUDIO_ENGINE_BEAT_FX_TARGET_CH1 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_CH2 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_BOTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (requested_mode != AUDIO_DELAY_FX_MODE_ECHO &&
        requested_mode != AUDIO_DELAY_FX_MODE_DELAY) {
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
            ESP_LOGW(TAG, "beat fx time effect deck %u buffer not allocated", (unsigned)deck);
        }
        /* A generic disable must retain the currently active mode so a DELAY
         * tail cannot accidentally acquire ECHO feedback while ringing out. */
        audio_delay_fx_mode_t mode = deck_enabled
            ? requested_mode
            : (audio_delay_fx_mode_t)atomic_load_u32(&s_beat_fx_echo_mode[deck]);
        atomic_store_bool(&s_beat_fx_echo_enabled[deck], deck_enabled);
        atomic_store_u32(&s_beat_fx_echo_delay_ms[deck], deck_enabled ? delay_ms : 0u);
        atomic_store_u32(&s_beat_fx_echo_mode[deck], (uint32_t)mode);
        /* No reset on switch-off: audio_delay_fx keeps the tail ringing and
         * the output mixer keeps processing until it decays. */
        audio_delay_fx_config_t config = {
            .enabled = deck_enabled,
            .mode = mode,
            .delay_ms = delay_ms,
            .wet_q15 = beat_fx_time_wet_from_depth(depth),
            .feedback_q15 = mode == AUDIO_DELAY_FX_MODE_ECHO
                ? beat_fx_echo_feedback_from_depth(depth)
                : 0u,
        };
        (void)publish_echo_command(deck, &config);
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
        audio_flanger_fx_config_t config = {
            .enabled = deck_enabled,
            .period_ms = period_ms,
            .depth_q15 = beat_fx_flanger_depth_q15(depth),
        };
        (void)publish_flanger_command(deck, &config);
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
    audio_pad_fx_config_t config = {
        .mode = mode,
        .pad = pad,
        .active = active,
    };
    __atomic_store_n(&s_pad_fx_command[deck], pack_pad_fx_command(config),
                     __ATOMIC_RELEASE);
    return ESP_OK;
}

esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                        uint8_t depth,
                                        uint32_t delay_ms,
                                        bool enabled)
{
    return audio_engine_set_beat_fx_time_effect(target,
                                                depth,
                                                delay_ms,
                                                AUDIO_DELAY_FX_MODE_ECHO,
                                                enabled);
}

esp_err_t audio_engine_set_beat_fx_delay(audio_engine_beat_fx_target_t target,
                                         uint8_t depth,
                                         uint32_t delay_ms,
                                         bool enabled)
{
    return audio_engine_set_beat_fx_time_effect(target,
                                                depth,
                                                delay_ms,
                                                AUDIO_DELAY_FX_MODE_DELAY,
                                                enabled);
}

esp_err_t audio_engine_set_master_trim(float gain)
{
    if (!(gain >= 0.0f)) {
        gain = 0.0f;
    } else if (gain > 1.0f) {
        gain = 1.0f;
    }
    master_trim_store(gain);
    return ESP_OK;
}

uint32_t audio_engine_get_output_sample_rate(void)
{
#if AE_PC
    return 0u;   /* no I2S output stage in the offline PC build */
#else
    return s_output_sample_rate;
#endif
}

float audio_engine_get_master_trim(void)
{
    return master_trim_load();
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
                      master_trim_load();
    }
    if (deck1_gain) {
        *deck1_gain = audio_mixer_fader_gain(channel_volume1) *
                      pregain_gain_from_raw(pregain1) *
                      xf1 *
                      audio_mixer_fader_gain(master_volume) *
                      master_trim_load();
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
        uint32_t filter_command = __atomic_load_n(
            &s_beat_fx_filter_command[deck], __ATOMIC_ACQUIRE);
        out_snapshot->beat_fx_filter_raw[deck] =
            (uint16_t)(filter_command & 0xFFFFu);
        out_snapshot->beat_fx_filter_enabled[deck] = atomic_load_bool(&s_beat_fx_filter_enabled[deck]);
        read_echo_command_status(deck,
                                 &out_snapshot->beat_fx_echo_enabled[deck],
                                 &out_snapshot->beat_fx_echo_delay_ms[deck],
                                 &out_snapshot->beat_fx_echo_mode[deck]);
        out_snapshot->beat_fx_echo_allocated[deck] = audio_delay_fx_is_allocated(&s_beat_fx_echo[deck]);
        uint32_t pad_command = __atomic_load_n(&s_pad_fx_command[deck],
                                               __ATOMIC_ACQUIRE);
        out_snapshot->pad_fx_active[deck] =
            unpack_pad_fx_command(pad_command).active &&
            pad_fx_kind_from_command(pad_command) != AUDIO_PAD_FX_KIND_NONE;
        out_snapshot->pad_fx_kind[deck] = pad_fx_kind_from_command(pad_command);
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
            engine_pitch_load(deck) * (1.0f + jog_bend_load(deck)) * 1000.0f;
        if (speed < 0.0f) speed = 0.0f;
        if (speed > 65535.0f) speed = 65535.0f;
        out_snapshot->effective_speed_permille[deck] = (uint16_t)(speed + 0.5f);
    }
    out_snapshot->master_trim = master_trim_load();
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
    out_snapshot->scratch_buffer_capacity = AE_TIMELINE_CAPACITY_FRAMES;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        audio_engine_state_t *eng = &s_engines[deck];
        out_snapshot->deck_active[deck] = atomic_load_bool(&eng->playing) &&
                                          !atomic_load_bool(&eng->paused);
        out_snapshot->ring_used[deck] = deck_pcm_used(deck);
        out_snapshot->pcm_timeline_active[deck] = timeline_active(deck);
        out_snapshot->pcm_timeline_history[deck] = timeline_active(deck)
            ? audio_pcm_timeline_history_frames(&s_pcm_timelines[deck]) : 0u;
        out_snapshot->pcm_timeline_future[deck] = timeline_active(deck)
            ? audio_pcm_timeline_future_frames(&s_pcm_timelines[deck]) : 0u;
        out_snapshot->pcm_timeline_generation[deck] = timeline_active(deck)
            ? audio_pcm_timeline_generation(&s_pcm_timelines[deck]) : 0u;
        out_snapshot->pcm_underrun_count[deck] = s_pcm_underrun_count[deck];
#if AE_FW
        out_snapshot->startup_waiting[deck] =
            atomic_load_bool(&s_start_waiting[deck]);
        out_snapshot->startup_wait_count[deck] =
            __atomic_load_n(&s_start_wait_count[deck], __ATOMIC_RELAXED);
#endif
        out_snapshot->loop_trim_wraps[deck] = s_loop_trim_wraps[deck];
        out_snapshot->loop_trim_dropped_max[deck] = s_loop_trim_dropped_max[deck];
        out_snapshot->loop_trim_dropped_total[deck] = s_loop_trim_dropped_total[deck];
        out_snapshot->loop_trim_clamped_total[deck] = s_loop_trim_clamped_total[deck];
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
        read_echo_command_status(deck,
                                 &out_snapshot->beat_fx_echo_enabled[deck],
                                 &out_snapshot->beat_fx_echo_delay_ms[deck],
                                 &out_snapshot->beat_fx_echo_mode[deck]);
        uint32_t pad_command = __atomic_load_n(&s_pad_fx_command[deck],
                                               __ATOMIC_ACQUIRE);
        out_snapshot->pad_fx_active[deck] =
            unpack_pad_fx_command(pad_command).active &&
            pad_fx_kind_from_command(pad_command) != AUDIO_PAD_FX_KIND_NONE;
    }
    out_snapshot->startup_prebuffer_frames =
#if AE_FW
        AE_START_PREBUFFER_FRAMES;
#else
        0u;
#endif
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
    out_snapshot->phase_mix_max_us = s_phase.mix_max_us;
    out_snapshot->phase_push_max_us = s_phase.push_max_us;
    out_snapshot->phase_monitor_max_us = s_phase.monitor_max_us;
    out_snapshot->phase_main_max_us = s_phase.main_max_us;
    out_snapshot->phase_codec_max_us = s_phase.codec_max_us;
    out_snapshot->phase_book_max_us = s_phase.book_max_us;
    out_snapshot->phase_head_max_us = s_phase.head_max_us;
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
    audio_engine_state_t *eng = &s_engines[AE_DECK_0];
    #if AE_FW
    if (!eng->loaded || !s_fw_preloads[deck].source) {
#else
    if (!eng->loaded || (!eng->fp && !eng->decoder_open)) {
#endif
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
    atomic_store_bool(&eng->eof, false);

    int16_t  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    uint32_t pcm_bytes   = 0u;
    uint32_t sample_rate = 0u;
    uint16_t channels    = 2u;

    while (true) {
        AE_LOCK();
        int samples = decode_one_frame(eng, pcm);
        if (samples > 0) eng->frames_since_seek += (uint64_t)samples;
        bool eof = atomic_load_bool(&eng->eof);
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
