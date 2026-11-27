#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t next_id;
    uint32_t active_id;
    bool busy;
    bool cancelled;
    bool remote_replacement_committed;
} ui_load_gate_t;

void ui_load_gate_reset(ui_load_gate_t *gate);
bool ui_load_gate_try_begin(ui_load_gate_t *gate, uint32_t *load_id);
bool ui_load_gate_is_current(const ui_load_gate_t *gate, uint32_t load_id);
bool ui_load_gate_finish(ui_load_gate_t *gate, uint32_t load_id);
void ui_load_gate_invalidate(ui_load_gate_t *gate);
/* Browser/source changes can cancel a download up to its deck replacement.
 * A physical media invalidation still uses ui_load_gate_invalidate(). */
void ui_load_gate_cancel_remote(ui_load_gate_t *gate);
bool ui_load_gate_commit_remote_replacement(ui_load_gate_t *gate, uint32_t load_id);
