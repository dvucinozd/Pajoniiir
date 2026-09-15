#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    LIBRARY_VALIDATION_GATE_IDLE = 0,
    LIBRARY_VALIDATION_GATE_ARMED,
    LIBRARY_VALIDATION_GATE_HOLDING,
    LIBRARY_VALIDATION_GATE_MEDIA_REMOVED,
    LIBRARY_VALIDATION_GATE_TIMED_OUT,
    LIBRARY_VALIDATION_GATE_CANCELED,
} library_validation_gate_state_t;

typedef struct {
    library_validation_gate_state_t state;
    uint32_t sequence;
    uint32_t timeout_ms;
} library_validation_gate_snapshot_t;

/* One-shot hardware-validation barrier. Normal product operation is a no-op
 * unless an authorized diagnostic client explicitly arms it while USB0 is
 * absent. The next PDB load pauses after its first bounded read and resumes on
 * media removal, cancellation or timeout. */
esp_err_t library_validation_gate_arm(uint32_t timeout_ms);
void library_validation_gate_cancel(void);
bool library_validation_gate_checkpoint(void);
void library_validation_gate_snapshot(library_validation_gate_snapshot_t *out);
const char *library_validation_gate_state_name(library_validation_gate_state_t state);

#if defined(LIBRARY_VALIDATION_GATE_HOST_TEST)
typedef void (*library_validation_gate_test_poll_hook_t)(void);
void library_validation_gate_test_reset(void);
void library_validation_gate_test_set_poll_hook(
    library_validation_gate_test_poll_hook_t hook);
#endif
