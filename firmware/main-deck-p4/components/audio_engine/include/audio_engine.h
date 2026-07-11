#pragma once
/*
 * audio_engine.h — MP3 decode + PCM output
 *
 * Decodes MP3 from USB drive using minimp3 (single-header, public domain).
 * Supports VBR seeking via Rekordbox PVBR seek table (400 file-byte offsets
 * stored in ANLZ0000.DAT), and pitch/tempo control via linear-interpolation
 * resampling (±10% rate; no key preservation — key-lock is Phase 10).
 *
 * Platform selection (compile-time defines):
 *   AUDIO_ENGINE_PC_TEST       — WAV file output (offline unit test)
 *   (neither)                  — firmware: progressive preload, decode task, codec/I2S output
 *
 * Typical call sequence:
 *   audio_engine_init();
 *   audio_engine_load(path, pvbr, duration_ms);
 *   audio_engine_play();
 *   audio_engine_set_pitch(raw_pitch);   // 0–16383, center 8192 = ±10%
 *   ...
 *   audio_engine_seek(position_ms);
 *   ...
 *   audio_engine_stop();
 */

#include <stdint.h>
#include <stdbool.h>
#include "audio_eq.h"
#include "audio_filter.h"
#include "audio_mixer.h"
#include "audio_pad_fx.h"

#if defined(AUDIO_ENGINE_PC_TEST)
    /* Stand-alone PC test build: provide ESP-IDF types without IDF headers */
    typedef int esp_err_t;
#   define ESP_OK               0
#   define ESP_FAIL            -1
#   define ESP_ERR_INVALID_ARG  0x102
#   define ESP_ERR_INVALID_STATE 0x103
#   define ESP_ERR_NO_MEM        0x101
#   define ESP_ERR_NOT_FOUND    0x105
#   define ESP_ERR_NOT_SUPPORTED 0x106
#else
#   include "esp_err.h"
#endif

#define AUDIO_PVBR_LEN  400u   /* entries in Rekordbox PVBR seek table */
#define AUDIO_ENGINE_DECK_COUNT 2u
#define AUDIO_ENGINE_COMPAT_DECK 0u

/*
 * Initialise the audio engine.
 * Sets up I2S buffers (firmware).
 * Must be called before any other function.
 */
esp_err_t audio_engine_init(void);

/*
 * Load a track for playback.
 *
 * @param mp3_path    Absolute path to the MP3 file (full path incl. drive letter on PC).
 * @param pvbr_400    Pointer to 400-entry uint32_t PVBR seek table, or NULL if absent.
 *                    When present, seek() uses it for instant byte-level positioning.
 * @param duration_ms Track duration from ANLZ beat-grid (0 = unknown).
 */
esp_err_t audio_engine_load(const char     *mp3_path,
                             const uint32_t *pvbr_400,
                             uint32_t        duration_ms);

/*
 * Start or resume playback.
 * Returns ESP_ERR_INVALID_STATE if no track is loaded.
 */
esp_err_t audio_engine_play(void);

/*
 * Pause playback (can be resumed with audio_engine_play).
 */
esp_err_t audio_engine_pause(void);

/*
 * Stop playback and unload the current track.
 * Frees file handles and resets position to 0.
 */
esp_err_t audio_engine_stop(void);

/*
 * Seek to a position in the current track.
 * Uses PVBR table when available (fast), otherwise scans from beginning (slow).
 *
 * @param position_ms  Target position in milliseconds.
 */
esp_err_t audio_engine_seek(uint32_t position_ms);

/*
 * Set playback pitch/rate.
 *
 * @param raw_pitch  Raw 14-bit value from CDJ pitch fader.
 *                   0     = +10% (faster)
 *                   8192  = ±0%  (normal)
 *                   16383 = -10% (slower)
 *
 * Percent = ((8192 - raw_pitch) / 8192.0) * 10.0
 * Factor = 1.0 + (Percent / 100.0)
 * NOTE: this changes tempo only — no key preservation (key-lock = Phase 10).
 */
void audio_engine_set_pitch(int16_t raw_pitch);
float audio_engine_raw_pitch_to_percent(int16_t raw_pitch);
void audio_engine_set_pitch_percent(float percent);

/*
 * Current audible playback position in the track (milliseconds), based on
 * output-consumed frames rather than the decoder read-ahead cursor.
 * Thread-safe.
 */
uint32_t audio_engine_position_ms(void);

bool audio_engine_is_playing(void);

/*
 * Transitional deck-aware API for the DDJ-FLX4 port.
 *
 * Deck 0 is the compatibility deck that owns codec/output startup. Deck 1
 * has deck-local load/play/pause/seek/pitch state and can participate in the
 * shared firmware output mixer once the compatibility output task is running.
 */
esp_err_t audio_engine_deck_load(uint8_t deck,
                                 const char *mp3_path,
                                 const uint32_t *pvbr_400,
                                 uint32_t duration_ms);
esp_err_t audio_engine_deck_play(uint8_t deck);
esp_err_t audio_engine_deck_pause(uint8_t deck);
esp_err_t audio_engine_deck_stop(uint8_t deck);
esp_err_t audio_engine_deck_seek(uint8_t deck, uint32_t position_ms);
void audio_engine_deck_set_pitch(uint8_t deck, int16_t raw_pitch);
void audio_engine_deck_set_pitch_percent(uint8_t deck, float percent);
/* Transient jog pitch-bend (nudge) while playing: a positive delta briefly
 * speeds the deck up, negative slows it, and the tempo springs back to the
 * fader setting when the jog stops. Used for manual beat matching. */
void audio_engine_deck_jog_nudge(uint8_t deck, int16_t delta);
/* Platter-hold (vinyl mode Phase 1): while held the deck output is silenced and
 * its playback position frozen, without changing the logical play state (LEDs
 * stay lit). Clearing it resumes forward playback instantly from the current
 * position. Set on jog-platter touch-down, cleared on release. */
void audio_engine_deck_set_hold(uint8_t deck, bool held);
/* Scratch playback (vinyl mode Phase 4). begin() enters scratch: the deck's
 * audio is produced by a jog-driven read head over the capture buffer (seeded at
 * the current playhead) instead of forward playback; move() feeds jog ticks to
 * the head velocity; end() converts the head position back to a track position,
 * seeks normal playback there and resumes forward. Gated behind
 * CONFIG_AUDIO_SCRATCH_ENABLED at the call site (deck_core). */
bool audio_engine_deck_scratch_begin(uint8_t deck);
void audio_engine_deck_scratch_move(uint8_t deck, int16_t delta);
void audio_engine_deck_scratch_end(uint8_t deck);
uint32_t audio_engine_deck_position_ms(uint8_t deck);
bool audio_engine_deck_is_playing(uint8_t deck);
uint16_t audio_engine_get_deck_peak(uint8_t deck);

typedef struct {
    uint16_t channel_volume[AUDIO_ENGINE_DECK_COUNT];
    uint16_t crossfader;
    uint16_t pregain[AUDIO_ENGINE_DECK_COUNT];
    float pregain_gain[AUDIO_ENGINE_DECK_COUNT];
    uint16_t eq[AUDIO_ENGINE_DECK_COUNT][AUDIO_EQ_BAND_COUNT];
    uint16_t filter[AUDIO_ENGINE_DECK_COUNT];
    uint16_t smart_cfx_filter_effective[AUDIO_ENGINE_DECK_COUNT];
    uint16_t beat_fx_filter_raw[AUDIO_ENGINE_DECK_COUNT];
    bool beat_fx_filter_enabled[AUDIO_ENGINE_DECK_COUNT];
    bool beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
    uint32_t beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
    bool beat_fx_echo_allocated[AUDIO_ENGINE_DECK_COUNT];
    bool pad_fx_active[AUDIO_ENGINE_DECK_COUNT];
    audio_pad_fx_kind_t pad_fx_kind[AUDIO_ENGINE_DECK_COUNT];
    float output_gain[AUDIO_ENGINE_DECK_COUNT];
    uint16_t deck_peak[AUDIO_ENGINE_DECK_COUNT];
    /* Display VU peak: instant attack, gentle decay, maintained by the output
     * task and read non-destructively. Unlike deck_peak (a raw running max the
     * FLX4 LED path drains via audio_engine_get_deck_peak), this never sticks
     * and is independent of that consumer, so the on-screen VU stays live. */
    uint16_t deck_peak_display[AUDIO_ENGINE_DECK_COUNT];
    /* Current playback speed per deck in per-mille (1000 = 1x), including the
     * pitch fader and the transient jog bend — lets the on-screen waveform track
     * the audio during a jog nudge instead of lagging at the fader speed. */
    uint16_t effective_speed_permille[AUDIO_ENGINE_DECK_COUNT];
    bool scratch_position_authoritative[AUDIO_ENGINE_DECK_COUNT];
    float master_trim;
    uint16_t master_volume;
    uint16_t headphone_mix;
    uint16_t headphone_level;
    bool master_cue_enabled;
    bool pfl_enabled[AUDIO_ENGINE_DECK_COUNT];
    bool smart_cfx_enabled;
    bool smart_fader_enabled;
    audio_mixer_limiter_stats_t limiter;
} audio_engine_mixer_snapshot_t;

typedef struct {
    bool output_codec_open;
    uint32_t output_sample_rate;
    uint32_t output_late_count;
    uint32_t output_late_max_us;
    uint32_t output_late_threshold_us;
    bool deck_active[AUDIO_ENGINE_DECK_COUNT];
    uint32_t ring_used[AUDIO_ENGINE_DECK_COUNT];
    uint32_t ring_capacity;
    bool pcm_timeline_active[AUDIO_ENGINE_DECK_COUNT];
    uint32_t pcm_timeline_history[AUDIO_ENGINE_DECK_COUNT];
    uint32_t pcm_timeline_future[AUDIO_ENGINE_DECK_COUNT];
    uint32_t pcm_timeline_generation[AUDIO_ENGINE_DECK_COUNT];
    uint32_t pcm_underrun_count[AUDIO_ENGINE_DECK_COUNT];
    uint32_t scratch_edge_hit_count[AUDIO_ENGINE_DECK_COUNT];
    bool scratch_active[AUDIO_ENGINE_DECK_COUNT];
    bool scratch_capture_frozen[AUDIO_ENGINE_DECK_COUNT];
    uint32_t scratch_buffer_used[AUDIO_ENGINE_DECK_COUNT];
    uint32_t scratch_buffer_capacity;
    uint32_t scratch_generation[AUDIO_ENGINE_DECK_COUNT];
    uint32_t scratch_head_back_frames[AUDIO_ENGINE_DECK_COUNT];
    uint32_t deck_sample_rate[AUDIO_ENGINE_DECK_COUNT];
    uint8_t deck_channels[AUDIO_ENGINE_DECK_COUNT];
    uint32_t deck_file_bytes[AUDIO_ENGINE_DECK_COUNT];
    uint8_t deck_load_progress[AUDIO_ENGINE_DECK_COUNT];
    bool beat_fx_echo_allocated[AUDIO_ENGINE_DECK_COUNT];
    bool beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
    uint32_t beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
    bool pad_fx_active[AUDIO_ENGINE_DECK_COUNT];
    audio_mixer_limiter_stats_t limiter;
    uint32_t usb_headphone_submitted_blocks;
    uint32_t usb_headphone_dropped_blocks;
    uint32_t usb_headphone_submitted_frames;
    uint32_t heap_free;
    uint32_t internal_free;
    uint32_t psram_free;
} audio_engine_diagnostics_snapshot_t;

esp_err_t audio_engine_set_channel_volume(uint8_t deck, uint16_t raw_volume);
esp_err_t audio_engine_set_crossfader(uint16_t raw_crossfader);
esp_err_t audio_engine_set_pregain(uint8_t deck, uint16_t raw_pregain);
uint16_t audio_engine_get_pregain(uint8_t deck);
esp_err_t audio_engine_set_eq(uint8_t deck, audio_eq_band_t band, uint16_t raw);
uint16_t audio_engine_get_eq(uint8_t deck, audio_eq_band_t band);
esp_err_t audio_engine_set_filter(uint8_t deck, uint16_t raw_filter);
uint16_t audio_engine_get_filter(uint8_t deck);

typedef enum {
    AUDIO_ENGINE_BEAT_FX_TARGET_CH1 = 0,
    AUDIO_ENGINE_BEAT_FX_TARGET_CH2 = 1,
    AUDIO_ENGINE_BEAT_FX_TARGET_BOTH = 2,
} audio_engine_beat_fx_target_t;

esp_err_t audio_engine_set_beat_fx_filter(audio_engine_beat_fx_target_t target,
                                          uint8_t depth,
                                          bool enabled);
esp_err_t audio_engine_set_beat_fx_flanger(audio_engine_beat_fx_target_t target,
                                           uint8_t depth,
                                           uint32_t period_ms,
                                           bool enabled);
esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                        uint8_t depth,
                                        uint32_t delay_ms,
                                        bool enabled);
esp_err_t audio_engine_set_pad_fx(uint8_t deck,
                                  audio_pad_fx_mode_t mode,
                                  uint8_t pad,
                                  bool active);
esp_err_t audio_engine_set_master_volume(uint16_t raw_volume);
uint16_t audio_engine_get_master_volume(void);
esp_err_t audio_engine_set_headphone_mix(uint16_t raw_mix);
uint16_t audio_engine_get_headphone_mix(void);
esp_err_t audio_engine_set_headphone_level(uint16_t raw_level);
uint16_t audio_engine_get_headphone_level(void);
esp_err_t audio_engine_set_master_trim(float gain);
float audio_engine_get_master_trim(void);
void audio_engine_get_output_gains(float *deck0_gain, float *deck1_gain);
esp_err_t audio_engine_toggle_pfl(uint8_t deck);
bool audio_engine_get_pfl_enabled(uint8_t deck);
esp_err_t audio_engine_toggle_smart_cfx(void);
bool audio_engine_get_smart_cfx_enabled(void);
esp_err_t audio_engine_toggle_smart_fader(void);
bool audio_engine_get_smart_fader_enabled(void);
esp_err_t audio_engine_toggle_master_cue(void);
bool audio_engine_get_master_cue_enabled(void);

typedef enum {
    AUDIO_HEADPHONE_MODE_MASTER_MONO = 0,
    AUDIO_HEADPHONE_MODE_CUE_MONO,
    AUDIO_HEADPHONE_MODE_SPLIT_MONO,
} audio_headphone_mode_t;

esp_err_t audio_engine_set_headphone_mode(audio_headphone_mode_t mode);
audio_headphone_mode_t audio_engine_get_headphone_mode(void);

esp_err_t audio_engine_set_cue_mode(uint8_t mode);
uint8_t audio_engine_get_cue_mode(void);
void audio_engine_get_mixer_snapshot(audio_engine_mixer_snapshot_t *out_snapshot);
void audio_engine_get_diagnostics_snapshot(audio_engine_diagnostics_snapshot_t *out_snapshot);

/*
 * Engine lifecycle state, for UI feedback (e.g. a "LOADING…" indicator).
 *   AE_IDLE     — no track loaded
 *   AE_LOADING  — preloading the MP3 from USB into PSRAM (not playable yet)
 *   AE_READY    — loaded/decodable, paused
 *   AE_PLAYING  — actively playing
 *   AE_ERROR    — load/decode failed; inspect audio_engine_last_error*()
 */
typedef enum { AE_IDLE = 0, AE_LOADING, AE_READY, AE_PLAYING, AE_ERROR } ae_state_t;

typedef struct {
    ae_state_t state;
    uint8_t load_progress;
    esp_err_t last_error;
    char last_error_text[64];
    bool loaded;
    bool playing;
    uint32_t position_ms;
} audio_engine_deck_status_t;

esp_err_t audio_engine_deck_get_status(uint8_t deck, audio_engine_deck_status_t *out);
esp_err_t audio_engine_stop_all(void);

ae_state_t audio_engine_get_state(void);
esp_err_t audio_engine_last_error(void);
const char *audio_engine_last_error_text(void);

/* Preload progress 0–100 while AE_LOADING (100 otherwise). */
uint8_t    audio_engine_load_progress(void);

/*
 * Set real-time audio loop boundaries.
 * When playing, if position reaches end_ms, it immediately seeks back to start_ms.
 */
void audio_engine_set_loop(uint32_t start_ms, uint32_t end_ms);

/*
 * Disable the active real-time loop.
 */
void audio_engine_clear_loop(void);

/*
 * Get the current loop boundaries and active state.
 */
void audio_engine_get_loop_state(bool *active, uint32_t *start_ms, uint32_t *end_ms);

esp_err_t audio_engine_deck_set_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms);
esp_err_t audio_engine_deck_clear_loop(uint8_t deck);
esp_err_t audio_engine_deck_get_loop_state(uint8_t deck,
                                           bool *active,
                                           uint32_t *start_ms,
                                           uint32_t *end_ms);


/* ── PC test helper (AUDIO_ENGINE_PC_TEST only) ───────────────────────────
 * Decode the loaded track to a WAV file.
 * max_duration_ms = 0 decodes the entire track.
 */
#if defined(AUDIO_ENGINE_PC_TEST)
esp_err_t audio_engine_decode_to_wav(const char *wav_path, uint32_t max_duration_ms);
void audio_engine_test_record_deck_peak(uint8_t deck, int16_t left, int16_t right);
void audio_engine_test_record_limiter_stats(const audio_mixer_limiter_stats_t *stats);
#endif
