#include "usb_host_recovery_arbiter.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

int main(void)
{
    usb_host_recovery_arbiter_t arb;
    usb_host_recovery_arbiter_init(&arb, 10u, 80u);
    CHECK(arb.active_port == USB_HOST_RECOVERY_PORT_NONE);
    CHECK(!usb_host_recovery_arbiter_request(&arb, 2u, USB_HOST_RECOVERY_REASON_MANUAL));
    CHECK(arb.invalid_requests == 1u);

    CHECK(usb_host_recovery_arbiter_request(&arb, 0u, USB_HOST_RECOVERY_REASON_ENUMERATION));
    CHECK(usb_host_recovery_arbiter_request(&arb, 0u, USB_HOST_RECOVERY_REASON_TRANSFER));
    CHECK(arb.coalesced_requests == 1u);
    CHECK(arb.ports[0].reason == USB_HOST_RECOVERY_REASON_TRANSFER);
    CHECK(usb_host_recovery_arbiter_request(&arb, 1u, USB_HOST_RECOVERY_REASON_MANUAL));

    uint8_t port = 99u;
    usb_host_recovery_reason_t reason = USB_HOST_RECOVERY_REASON_NONE;
    CHECK(usb_host_recovery_arbiter_acquire(&arb, 0u, &port, &reason));
    CHECK(port == 0u);
    CHECK(reason == USB_HOST_RECOVERY_REASON_TRANSFER);
    CHECK(!usb_host_recovery_arbiter_acquire(&arb, 0u, NULL, NULL));
    CHECK(!usb_host_recovery_arbiter_complete(&arb, 1u, true, 0u));
    CHECK(usb_host_recovery_arbiter_complete(&arb, 0u, false, 100u));
    CHECK(usb_host_recovery_arbiter_retry_delay(&arb, 0u) == 10u);

    CHECK(usb_host_recovery_arbiter_acquire(&arb, 100u, &port, &reason));
    CHECK(port == 1u);
    CHECK(usb_host_recovery_arbiter_complete(&arb, 1u, true, 100u));
    CHECK(!usb_host_recovery_arbiter_acquire(&arb, 109u, NULL, NULL));
    CHECK(usb_host_recovery_arbiter_acquire(&arb, 110u, &port, NULL));
    CHECK(port == 0u);
    CHECK(usb_host_recovery_arbiter_complete(&arb, 0u, false, 110u));
    CHECK(usb_host_recovery_arbiter_retry_delay(&arb, 0u) == 20u);
    CHECK(!usb_host_recovery_arbiter_acquire(&arb, 129u, NULL, NULL));
    CHECK(usb_host_recovery_arbiter_acquire(&arb, 130u, &port, NULL));
    CHECK(usb_host_recovery_arbiter_complete(&arb, 0u, false, 130u));
    CHECK(usb_host_recovery_arbiter_retry_delay(&arb, 0u) == 40u);
    CHECK(usb_host_recovery_arbiter_acquire(&arb, 170u, &port, NULL));
    CHECK(usb_host_recovery_arbiter_complete(&arb, 0u, false, 170u));
    CHECK(usb_host_recovery_arbiter_retry_delay(&arb, 0u) == 80u);
    CHECK(usb_host_recovery_arbiter_acquire(&arb, 250u, &port, NULL));
    CHECK(usb_host_recovery_arbiter_complete(&arb, 0u, false, 250u));
    CHECK(usb_host_recovery_arbiter_retry_delay(&arb, 0u) == 80u);
    CHECK(usb_host_recovery_arbiter_cancel(&arb, 0u));
    CHECK(!arb.ports[0].pending);

    usb_host_recovery_arbiter_init(&arb, 1u, 16u);
    unsigned served[2] = {0u, 0u};
    for (unsigned i = 0u; i < 1000u; ++i) {
        CHECK(usb_host_recovery_arbiter_request(
            &arb, 0u, USB_HOST_RECOVERY_REASON_ENUMERATION));
        CHECK(usb_host_recovery_arbiter_request(
            &arb, 1u, USB_HOST_RECOVERY_REASON_TRANSFER));
        CHECK(usb_host_recovery_arbiter_acquire(&arb, i, &port, NULL));
        CHECK(port < 2u);
        served[port]++;
        CHECK(!usb_host_recovery_arbiter_acquire(&arb, i, NULL, NULL));
        CHECK(usb_host_recovery_arbiter_complete(&arb, port, true, i));
    }
    CHECK(served[0] == 500u);
    CHECK(served[1] == 500u);
    CHECK(arb.active_port == USB_HOST_RECOVERY_PORT_NONE);

    printf("USB host recovery arbiter: %u checks passed\n", checks);
    return 0;
}
