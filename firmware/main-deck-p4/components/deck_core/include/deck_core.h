#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "control_link.h"

// ─── Performance modes (MODE button cycles through these) ────────────────────

typedef enum {
    PERF_MODE_HOT_CUE = 0,
    PERF_MODE_LOOP_ROLL,
    PERF_MODE_BEAT_JUMP,
    PERF_MODE_KEY_SHIFT,
    PERF_MODE_COUNT,
} perf_mode_t;

// ─── Deck state (read-only snapshot for UI / audio_engine) ───────────────────

typedef struct {
    bool          playing;
    uint32_t      position_ms;
    uint32_t      cue_point_ms;
    int16_t       pitch;          // 0–16383, center = 8192, from S3 ADC
    perf_mode_t   perf_mode;
    bool          master_tempo;
    bool          control_link_connected;
    uint32_t      last_heartbeat_age_ms;
} deck_state_t;

// ─── Public API ───────────────────────────────────────────────────────────────

// Create the ctrl_event_queue and start the deck task.
// Returns the queue handle — pass to control_link_init().
esp_err_t deck_core_init(QueueHandle_t *ctrl_event_queue_out);

// Thread-safe snapshot of the current deck state.
deck_state_t deck_core_get_state(void);

// Queue a control event (from touch screen or other source).
esp_err_t deck_core_queue_event(const ctrl_event_t *ev);

// Reset the deck state synchronously (on track load).
void deck_core_reset(void);
