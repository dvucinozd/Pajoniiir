#include "firmware_boot_gate.h"

#include <stdio.h>

static int failures;
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; \
} } while (0)

static void test_no_link_never_confirms(void)
{
    firmware_boot_gate_t gate;
    firmware_boot_gate_init(&gate, 0x1234u);
    firmware_boot_gate_set_critical_tasks_alive(&gate, true);
    CHECK(!firmware_boot_gate_ready(&gate));
}

static void test_early_and_wrong_ack_are_rejected(void)
{
    firmware_boot_gate_t gate;
    firmware_boot_gate_init(&gate, 0xBEEFu);
    CHECK(!firmware_boot_gate_observe_p4_ack(&gate, 0xBEEFu));
    firmware_boot_gate_set_critical_tasks_alive(&gate, true);
    CHECK(!firmware_boot_gate_observe_p4_ack(&gate, 0xBEEEu));
    CHECK(!firmware_boot_gate_ready(&gate));
}

static void test_exact_ack_after_liveness_confirms(void)
{
    firmware_boot_gate_t gate;
    firmware_boot_gate_init(&gate, 0x8001u);
    firmware_boot_gate_set_critical_tasks_alive(&gate, true);
    CHECK(firmware_boot_gate_observe_p4_ack(&gate, 0x8001u));
    CHECK(firmware_boot_gate_ready(&gate));
    firmware_boot_gate_set_critical_tasks_alive(&gate, false);
    CHECK(!firmware_boot_gate_ready(&gate));
}

static void test_zero_challenge_is_not_a_default_wire_value(void)
{
    firmware_boot_gate_t gate;
    firmware_boot_gate_init(&gate, 0u);
    CHECK(gate.challenge == 1u);
}

int main(void)
{
    test_no_link_never_confirms();
    test_early_and_wrong_ack_are_rejected();
    test_exact_ack_after_liveness_confirms();
    test_zero_challenge_is_not_a_default_wire_value();
    printf("TESTS_RUN=%u\n", checks);
    if (failures == 0) {
        puts("firmware boot gate tests passed");
        return 0;
    }
    return 1;
}
