#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    AUDIO_LOAD_VALIDATION_GATE_IDLE = 0,
    AUDIO_LOAD_VALIDATION_GATE_ARMED,
    AUDIO_LOAD_VALIDATION_GATE_HOLDING,
    AUDIO_LOAD_VALIDATION_GATE_MEDIA_REMOVED,
    AUDIO_LOAD_VALIDATION_GATE_TIMED_OUT,
    AUDIO_LOAD_VALIDATION_GATE_CANCELED,
} audio_load_validation_gate_state_t;

typedef struct {
    audio_load_validation_gate_state_t state;
    uint32_t sequence;
    uint32_t timeout_ms;
    uint8_t deck;
} audio_load_validation_gate_snapshot_t;

/* One-shot hardware-validation barrier for Group G. Normal audio loading is a
 * no-op unless an authorized diagnostic client explicitly arms one deck. The
 * selected loader pauses after its first bounded compressed-cache read and
 * resumes on media removal, cancellation or timeout. */
esp_err_t audio_load_validation_gate_arm(uint8_t deck, uint32_t timeout_ms);
void audio_load_validation_gate_cancel(void);
bool audio_load_validation_gate_checkpoint(uint8_t deck);
void audio_load_validation_gate_snapshot(
    audio_load_validation_gate_snapshot_t *out);
const char *audio_load_validation_gate_state_name(
    audio_load_validation_gate_state_t state);

#if defined(AUDIO_LOAD_VALIDATION_GATE_HOST_TEST)
typedef void (*audio_load_validation_gate_test_poll_hook_t)(void);
void audio_load_validation_gate_test_reset(void);
void audio_load_validation_gate_test_set_poll_hook(
    audio_load_validation_gate_test_poll_hook_t hook);
#endif
