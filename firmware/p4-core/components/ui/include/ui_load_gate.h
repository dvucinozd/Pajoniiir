#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t next_id;
    uint32_t active_id;
    bool busy;
    bool cancelled;
} ui_load_gate_t;

void ui_load_gate_reset(ui_load_gate_t *gate);
bool ui_load_gate_try_begin(ui_load_gate_t *gate, uint32_t *load_id);
bool ui_load_gate_is_current(const ui_load_gate_t *gate, uint32_t load_id);
bool ui_load_gate_finish(ui_load_gate_t *gate, uint32_t load_id);
void ui_load_gate_invalidate(ui_load_gate_t *gate);
