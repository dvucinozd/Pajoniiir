#include "controller_usb_recovery_gate.h"

#include <stdio.h>

static int failures;
static unsigned tests_run;

#define CHECK(expr) do { \
    tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #expr); \
        failures++; \
    } \
} while (0)

int main(void)
{
    controller_usb_recovery_gate_t gate;
    controller_usb_recovery_gate_init(&gate);
    CHECK(!controller_usb_recovery_gate_pending(&gate));
    CHECK(controller_usb_recovery_gate_fault_epochs(&gate) == 0u);

    CHECK(controller_usb_recovery_gate_begin_fault(&gate));
    CHECK(controller_usb_recovery_gate_pending(&gate));
    CHECK(controller_usb_recovery_gate_fault_epochs(&gate) == 1u);
    CHECK(!controller_usb_recovery_gate_begin_fault(&gate));
    CHECK(controller_usb_recovery_gate_fault_epochs(&gate) == 1u);

    controller_usb_recovery_gate_complete(&gate);
    CHECK(!controller_usb_recovery_gate_pending(&gate));
    CHECK(controller_usb_recovery_gate_begin_fault(&gate));
    CHECK(controller_usb_recovery_gate_fault_epochs(&gate) == 2u);

    controller_usb_recovery_gate_cancel(&gate);
    CHECK(!controller_usb_recovery_gate_pending(&gate));
    CHECK(controller_usb_recovery_gate_fault_epochs(&gate) == 2u);

    controller_usb_recovery_gate_init(NULL);
    controller_usb_recovery_gate_cancel(NULL);
    controller_usb_recovery_gate_complete(NULL);
    CHECK(!controller_usb_recovery_gate_begin_fault(NULL));
    CHECK(!controller_usb_recovery_gate_pending(NULL));
    CHECK(controller_usb_recovery_gate_fault_epochs(NULL) == 0u);

    printf("TESTS_RUN=%u\n", tests_run);
    return failures == 0 ? 0 : 1;
}
