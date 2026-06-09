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
 * Deck 0 delegates to the current single global engine. Deck 1 returns
 * ESP_ERR_NOT_SUPPORTED for mutating operations until Phase 4 splits the
 * engine state and output mixer.
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
uint32_t audio_engine_deck_position_ms(uint8_t deck);
bool audio_engine_deck_is_playing(uint8_t deck);

esp_err_t audio_engine_set_channel_volume(uint8_t deck, uint16_t raw_volume);
esp_err_t audio_engine_set_crossfader(uint16_t raw_crossfader);
void audio_engine_get_output_gains(float *deck0_gain, float *deck1_gain);
esp_err_t audio_engine_toggle_pfl(uint8_t deck);
bool audio_engine_get_pfl_enabled(uint8_t deck);

/*
 * Engine lifecycle state, for UI feedback (e.g. a "LOADING…" indicator).
 *   AE_IDLE     — no track loaded
 *   AE_LOADING  — preloading the MP3 from USB into PSRAM (not playable yet)
 *   AE_READY    — loaded/decodable, paused
 *   AE_PLAYING  — actively playing
 *   AE_ERROR    — load/decode failed; inspect audio_engine_last_error*()
 */
typedef enum { AE_IDLE = 0, AE_LOADING, AE_READY, AE_PLAYING, AE_ERROR } ae_state_t;

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


/* ── PC test helper (AUDIO_ENGINE_PC_TEST only) ───────────────────────────
 * Decode the loaded track to a WAV file.
 * max_duration_ms = 0 decodes the entire track.
 */
#if defined(AUDIO_ENGINE_PC_TEST)
esp_err_t audio_engine_decode_to_wav(const char *wav_path, uint32_t max_duration_ms);
#endif
