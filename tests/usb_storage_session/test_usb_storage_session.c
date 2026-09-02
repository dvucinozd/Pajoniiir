#include "usb_storage_session.h"

#include <stdio.h>

static int s_failures;
static unsigned s_checks;

#define CHECK(expr) do {                                                     \
    s_checks++;                                                              \
    if (!(expr)) {                                                           \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);               \
        s_failures++;                                                        \
    }                                                                        \
} while (0)

static void test_primary_lifecycle(void)
{
    printf("== primary connect, mount and disconnect ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(!session.connected);
    CHECK(!session.mounted);
    CHECK(session.epoch == 0u);
    CHECK(usb_storage_session_on_connect(&session, 4u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t epoch = session.epoch;
    CHECK(epoch == 1u);
    CHECK(session.dev_addr == 4u);
    CHECK(usb_storage_session_bind_handle(&session, epoch, 4u, 0xA0u));
    CHECK(session.accepted_handle == 0xA0u);
    CHECK(usb_storage_session_commit_mounted(&session, epoch, 4u));
    CHECK(session.mounted);
    CHECK(usb_storage_session_on_disconnect(&session, 0xA0u) ==
          USB_STORAGE_DISCONNECT_ACCEPTED);
    CHECK(!session.connected);
    CHECK(!session.mounted);
    CHECK(session.accepted_handle == 0u);
    CHECK(session.epoch == epoch + 1u);
}

static void test_secondary_disconnect_cannot_remove_primary(void)
{
    printf("== ignored secondary disconnect preserves mounted primary ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(usb_storage_session_on_connect(&session, 4u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t epoch = session.epoch;
    CHECK(usb_storage_session_bind_handle(&session, epoch, 4u, 0xA0u));
    CHECK(usb_storage_session_commit_mounted(&session, epoch, 4u));

    CHECK(usb_storage_session_on_connect(&session, 7u) ==
          USB_STORAGE_CONNECT_IGNORED_SECONDARY);
    CHECK(session.epoch == epoch);
    CHECK(usb_storage_session_on_disconnect(&session, 0xB0u) ==
          USB_STORAGE_DISCONNECT_IGNORED_FOREIGN);
    CHECK(session.connected);
    CHECK(session.mounted);
    CHECK(session.dev_addr == 4u);
    CHECK(session.accepted_handle == 0xA0u);

    CHECK(usb_storage_session_on_disconnect(&session, 0xA0u) ==
          USB_STORAGE_DISCONNECT_ACCEPTED);
    CHECK(!session.connected);
}

static void test_disconnect_during_opening_invalidates_late_completion(void)
{
    printf("== disconnect during opening invalidates stale completion ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(usb_storage_session_on_connect(&session, 2u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t opening_epoch = session.epoch;
    CHECK(session.accepted_handle == 0u);

    CHECK(usb_storage_session_on_disconnect(&session, 0xC0u) ==
          USB_STORAGE_DISCONNECT_ACCEPTED);
    CHECK(!usb_storage_session_matches(&session, opening_epoch, 2u));
    CHECK(!usb_storage_session_bind_handle(
        &session, opening_epoch, 2u, 0xC0u));
    CHECK(!usb_storage_session_commit_mounted(
        &session, opening_epoch, 2u));
    CHECK(!session.mounted);
}

static void test_duplicate_events_are_idempotent(void)
{
    printf("== duplicate connect and disconnect are idempotent ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(usb_storage_session_on_connect(&session, 5u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t epoch = session.epoch;
    CHECK(usb_storage_session_on_connect(&session, 5u) ==
          USB_STORAGE_CONNECT_DUPLICATE);
    CHECK(session.epoch == epoch);
    CHECK(usb_storage_session_bind_handle(&session, epoch, 5u, 0xD0u));

    CHECK(usb_storage_session_on_disconnect(&session, 0xD0u) ==
          USB_STORAGE_DISCONNECT_ACCEPTED);
    const uint32_t disconnected_epoch = session.epoch;
    CHECK(usb_storage_session_on_disconnect(&session, 0xD0u) ==
          USB_STORAGE_DISCONNECT_ALREADY_INACTIVE);
    CHECK(session.epoch == disconnected_epoch);
}

static void test_new_address_replaces_stale_unbound_connect(void)
{
    printf("== stable reconnect replaces stale unbound enumeration address ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(usb_storage_session_on_connect(&session, 3u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t stale_epoch = session.epoch;
    CHECK(session.accepted_handle == 0u);
    CHECK(!session.mounted);

    CHECK(usb_storage_session_on_connect(&session, 4u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    CHECK(session.connected);
    CHECK(session.dev_addr == 4u);
    CHECK(session.epoch > stale_epoch);
    CHECK(!usb_storage_session_bind_handle(
        &session, stale_epoch, 3u, 0xC0u));
    CHECK(usb_storage_session_bind_handle(
        &session, session.epoch, 4u, 0xC1u));
    CHECK(usb_storage_session_commit_mounted(
        &session, session.epoch, 4u));
}

static void test_failed_mount_can_retry_without_changing_session(void)
{
    printf("== failed mount releases handle and retries in same epoch ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(usb_storage_session_on_connect(&session, 6u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t epoch = session.epoch;
    CHECK(usb_storage_session_bind_handle(&session, epoch, 6u, 0xE0u));
    usb_storage_session_release_handle(&session, 0xE0u);
    CHECK(session.connected);
    CHECK(!session.mounted);
    CHECK(session.epoch == epoch);
    CHECK(session.accepted_handle == 0u);

    CHECK(usb_storage_session_bind_handle(&session, epoch, 6u, 0xE1u));
    CHECK(usb_storage_session_commit_mounted(&session, epoch, 6u));
    CHECK(session.mounted);
}

static void test_stale_previous_session_cannot_mutate_reconnect(void)
{
    printf("== stale prior-session callbacks cannot mutate reconnect ==\n");
    usb_storage_session_t session;
    usb_storage_session_reset(&session);

    CHECK(usb_storage_session_on_connect(&session, 3u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t first_epoch = session.epoch;
    CHECK(usb_storage_session_bind_handle(&session, first_epoch, 3u, 0xF0u));
    CHECK(usb_storage_session_on_disconnect(&session, 0xF0u) ==
          USB_STORAGE_DISCONNECT_ACCEPTED);

    CHECK(usb_storage_session_on_connect(&session, 3u) ==
          USB_STORAGE_CONNECT_ACCEPTED);
    const uint32_t second_epoch = session.epoch;
    CHECK(second_epoch > first_epoch);
    CHECK(usb_storage_session_bind_handle(&session, second_epoch, 3u, 0xF1u));
    CHECK(usb_storage_session_commit_mounted(&session, second_epoch, 3u));

    CHECK(!usb_storage_session_bind_handle(
        &session, first_epoch, 3u, 0xF0u));
    CHECK(!usb_storage_session_commit_mounted(
        &session, first_epoch, 3u));
    CHECK(usb_storage_session_on_disconnect(&session, 0xF0u) ==
          USB_STORAGE_DISCONNECT_IGNORED_FOREIGN);
    CHECK(session.connected);
    CHECK(session.mounted);
    CHECK(session.accepted_handle == 0xF1u);
}

int main(void)
{
    test_primary_lifecycle();
    test_secondary_disconnect_cannot_remove_primary();
    test_disconnect_during_opening_invalidates_late_completion();
    test_duplicate_events_are_idempotent();
    test_new_address_replaces_stale_unbound_connect();
    test_failed_mount_can_retry_without_changing_session();
    test_stale_previous_session_cannot_mutate_reconnect();

    printf("TESTS_RUN=%u\n", s_checks);
    if (s_failures == 0) {
        puts("usb_storage_session tests passed");
        return 0;
    }
    printf("usb_storage_session tests FAILED (%d)\n", s_failures);
    return 1;
}
