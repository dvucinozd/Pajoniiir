#include "ui_load_gate.h"

#include <string.h>

static uint32_t next_nonzero(uint32_t value)
{
    value++;
    return value == 0u ? 1u : value;
}

void ui_load_gate_reset(ui_load_gate_t *gate)
{
    if (gate) memset(gate, 0, sizeof(*gate));
}

bool ui_load_gate_try_begin(ui_load_gate_t *gate, uint32_t *load_id)
{
    if (!gate || gate->busy) return false;
    gate->next_id = next_nonzero(gate->next_id);
    gate->active_id = gate->next_id;
    gate->busy = true;
    gate->cancelled = false;
    if (load_id) *load_id = gate->active_id;
    return true;
}

bool ui_load_gate_is_current(const ui_load_gate_t *gate, uint32_t load_id)
{
    return gate && gate->busy && !gate->cancelled &&
           load_id != 0u && load_id == gate->active_id;
}

bool ui_load_gate_finish(ui_load_gate_t *gate, uint32_t load_id)
{
    if (!gate || !gate->busy || load_id == 0u || load_id != gate->active_id) {
        return false;
    }
    gate->busy = false;
    gate->cancelled = false;
    return true;
}

void ui_load_gate_invalidate(ui_load_gate_t *gate)
{
    if (!gate) return;
    if (gate->busy) gate->cancelled = true;
}
