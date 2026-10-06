#include "ui_event_counter.h"

void ui_event_counter_reset(ui_event_counter_t *counter)
{
    if (counter) {
        __atomic_store_n(&counter->requested, 0u, __ATOMIC_RELEASE);
    }
}

uint32_t ui_event_counter_request(ui_event_counter_t *counter)
{
    if (!counter) {
        return 0u;
    }
    return __atomic_add_fetch(&counter->requested, 1u, __ATOMIC_ACQ_REL);
}

uint32_t ui_event_counter_sample(const ui_event_counter_t *counter)
{
    if (!counter) {
        return 0u;
    }
    return __atomic_load_n(&counter->requested, __ATOMIC_ACQUIRE);
}
