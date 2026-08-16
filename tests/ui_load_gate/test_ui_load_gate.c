#include "ui_load_gate.h"

#include <assert.h>
#include <stdio.h>

static unsigned s_checks;
#define CHECK(expr) do { s_checks++; assert(expr); } while (0)

static void test_cancelled_worker_must_retire_before_new_load(void)
{
    ui_load_gate_t gate;
    ui_load_gate_reset(&gate);
    uint32_t old_id = 0u;
    CHECK(ui_load_gate_try_begin(&gate, &old_id));
    CHECK(ui_load_gate_is_current(&gate, old_id));

    ui_load_gate_invalidate(&gate); /* USB removal */
    CHECK(!ui_load_gate_is_current(&gate, old_id));
    CHECK(gate.busy);
    uint32_t new_id = 0u;
    CHECK(!ui_load_gate_try_begin(&gate, &new_id));
    CHECK(ui_load_gate_finish(&gate, old_id));
    CHECK(ui_load_gate_try_begin(&gate, &new_id)); /* reconnect + LOAD */
    CHECK(new_id != old_id);
    CHECK(!ui_load_gate_finish(&gate, old_id));
    CHECK(ui_load_gate_is_current(&gate, new_id));
    CHECK(ui_load_gate_finish(&gate, new_id));
    CHECK(!gate.busy);
}

static void test_single_flight_and_wrap_never_issue_zero(void)
{
    ui_load_gate_t gate = { .next_id = UINT32_MAX };
    uint32_t id = 0u;
    CHECK(ui_load_gate_try_begin(&gate, &id));
    CHECK(id == 1u);
    CHECK(!ui_load_gate_try_begin(&gate, NULL));
    CHECK(ui_load_gate_finish(&gate, id));
}

int main(void)
{
    test_cancelled_worker_must_retire_before_new_load();
    test_single_flight_and_wrap_never_issue_zero();
    printf("TESTS_RUN=%u\n", s_checks);
    puts("ui_load_gate tests passed");
    return 0;
}
