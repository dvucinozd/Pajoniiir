#pragma once

/*
 * P4 master-output microSD recorder — lifecycle and real-time boundary.
 *
 * Records the exact post-limiter stereo master bus. The recorder is optional
 * and strictly subordinate to playback: the PSRAM ring is allocated only at
 * start() behind a free-PSRAM check, and any failure stops the recording while
 * leaving MAIN audio, cue, UI and controller operation running. The audio
 * output task only ever calls audio_recorder_push_master(), which copies one
 * already-rendered block and returns immediately.
 *
 * NOTE: this slice provides the state machine, PSRAM ring lifecycle and writer
 * task draining to a counting placeholder sink. Real microSD file handling
 * (WAV segments, checkpoint/finalize, `.part` recovery) and the shared SD I/O
 * arbiter are added in later slices.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_RECORDER_STOPPED = 0,
    AUDIO_RECORDER_STARTING,
    AUDIO_RECORDER_RECORDING,
    AUDIO_RECORDER_STOPPING,
    AUDIO_RECORDER_ERROR,
} audio_recorder_state_t;

typedef struct {
    audio_recorder_state_t state;
    uint32_t sample_rate;      /* active recording rate (0 when stopped) */
    uint32_t ring_capacity;    /* ring slots allocated */
    uint32_t ring_used;        /* slots currently occupied */
    uint32_t ring_high_water;  /* max slots used since start */
    uint32_t dropped_blocks;   /* full-ring producer drops */
    uint64_t dropped_frames;   /* frames lost to drops */
    uint64_t bytes_written;    /* PCM bytes drained to the sink */
    uint64_t frames_written;   /* stereo frames drained to the sink */
    esp_err_t last_error;      /* last error that stopped/failed a session */
} audio_recorder_status_t;

/* One-time init (idempotent). Does not start recording. */
esp_err_t audio_recorder_init(void);

/* Start recording at `sample_rate` Hz. Allowed only from STOPPED. Allocates the
 * PSRAM ring behind a free-PSRAM check and starts the writer task. Returns
 * ESP_ERR_NO_MEM (state unchanged, playback untouched) if PSRAM is short. */
esp_err_t audio_recorder_start(uint32_t sample_rate);

/* Stop recording: drain the ring, release resources and return to STOPPED. Safe
 * to call from any state (idempotent when already stopped). */
esp_err_t audio_recorder_stop(void);

/* Real-time producer boundary: copy one rendered master block into the ring and
 * return immediately. No allocation, logging, file or blocking work. Returns
 * false (and advances drop counters) when not RECORDING or the ring is full. */
bool audio_recorder_push_master(const int16_t *stereo, size_t frames,
                                uint32_t sample_rate);

/* Copy a consistent status snapshot for diagnostics / UI / API. */
esp_err_t audio_recorder_get_status(audio_recorder_status_t *out);

/* Current state (lock-free read). */
audio_recorder_state_t audio_recorder_get_state(void);

#ifdef __cplusplus
}
#endif
