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

#define PERF_MODE_BEAT_LOOP PERF_MODE_LOOP_ROLL

// ─── Deck state (read-only snapshot for UI / audio_engine) ───────────────────

#define DECK_CORE_DECK_COUNT 2
#define DECK_CORE_COMPAT_DECK CTRL_DECK_1

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
// Compatibility helper: returns Deck 1.
deck_state_t deck_core_get_state(void);

// Thread-safe snapshot of one deck state.
deck_state_t deck_core_get_deck_state(uint8_t deck);

// Queue a control event (from touch screen or other source).
esp_err_t deck_core_queue_event(const ctrl_event_t *ev);

// Reset the deck state synchronously (on track load).
// Compatibility helper: resets Deck 1.
void deck_core_reset(void);

// Reset one deck state synchronously.
void deck_core_reset_deck(uint8_t deck);

#if defined(DECK_CORE_PC_TEST)
void deck_core_test_reset(void);
void deck_core_test_apply_event(const ctrl_event_t *ev);
deck_state_t deck_core_test_get_deck_state(uint8_t deck);
bool deck_core_test_should_log_deferred_mixer_value(uint8_t id, uint16_t value);
bool deck_core_test_should_log_deferred_button(uint8_t id, int16_t value);
#endif
