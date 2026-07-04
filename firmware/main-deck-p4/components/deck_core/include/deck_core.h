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
    int16_t       pitch_centipercent; // effective tempo adjust in 0.01% units
    uint16_t      tempo_range_percent; // selected tempo fader range, e.g. 6/10/16
    perf_mode_t   perf_mode;
    ctrl_pad_mode_t pad_mode;
    bool          sync_enabled;
    bool          sync_master;
    bool          quantize_enabled;
    bool          censor_active;
    bool          master_tempo;
    bool          control_link_connected;
    uint32_t      last_heartbeat_age_ms;
} deck_state_t;

typedef enum {
    DECK_CORE_BEAT_FX_NONE = 0,
    DECK_CORE_BEAT_FX_FILTER,
    DECK_CORE_BEAT_FX_ECHO,
    DECK_CORE_BEAT_FX_COUNT,
} deck_core_beat_fx_effect_t;

typedef enum {
    DECK_CORE_BEAT_FX_BEAT_1_4 = 0,
    DECK_CORE_BEAT_FX_BEAT_1_2,
    DECK_CORE_BEAT_FX_BEAT_1,
    DECK_CORE_BEAT_FX_BEAT_2,
    DECK_CORE_BEAT_FX_BEAT_4,
    DECK_CORE_BEAT_FX_BEAT_COUNT,
} deck_core_beat_fx_beat_t;

typedef struct {
    deck_core_beat_fx_effect_t effect;
    deck_core_beat_fx_beat_t beat;
    ctrl_beat_fx_target_t target;
    uint8_t depth;
    bool enabled;
} deck_core_beat_fx_state_t;

static inline float deck_core_pitch_percent(const deck_state_t *state)
{
    return state ? ((float)state->pitch_centipercent / 100.0f) : 0.0f;
}

// ─── Public API ───────────────────────────────────────────────────────────────

// Create the ctrl_event_queue and start the deck task.
// Returns the queue handle — pass to control_link_init().
esp_err_t deck_core_init(QueueHandle_t *ctrl_event_queue_out);

// Thread-safe snapshot of the current deck state.
// Compatibility helper: returns Deck 1.
deck_state_t deck_core_get_state(void);

// Thread-safe snapshot of one deck state.
deck_state_t deck_core_get_deck_state(uint8_t deck);

// Snapshot of the global Beat FX state. Beat FX DSP is not applied yet; this is
// exposed for low-rate diagnostics and controller smoke verification.
deck_core_beat_fx_state_t deck_core_get_beat_fx_state(void);

// Loop region for waveform display. `active` = a full loop (in+out) is set;
// `armed` = loop-in pressed and waiting for loop-out (highlight from start_ms to
// the live playhead). `end_ms` is only meaningful when `active`.
typedef struct {
    bool active;
    bool armed;
    uint32_t start_ms;
    uint32_t end_ms;
} deck_core_loop_display_t;

deck_core_loop_display_t deck_core_get_loop_display(uint8_t deck);

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
void deck_core_test_flush_ui_commands(void);
deck_state_t deck_core_test_get_deck_state(uint8_t deck);
deck_core_beat_fx_state_t deck_core_test_get_beat_fx_state(void);
bool deck_core_test_should_log_deferred_mixer_value(uint8_t id, uint16_t value);
bool deck_core_test_should_log_deferred_button(uint8_t id, int16_t value);
#endif
