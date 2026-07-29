#include "usb_storage_recovery.h"

#include <stdint.h>
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

#define FAST_TICKS  900u
#define FAST_LIMIT  8u
#define SLOW_TICKS  30000u

static bool due(const usb_storage_recovery_t *recovery, uint32_t now)
{
    return usb_storage_recovery_cycle_due(
        recovery, now, FAST_TICKS, FAST_LIMIT, SLOW_TICKS);
}

static void test_cold_boot_recovery_continues_until_connect(void)
{
    puts("== cold boot recovery continues until connect ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, false, 0u, 100u, 1u);

    CHECK(recovery.armed);
    CHECK(!recovery.session_connected);
    CHECK(recovery.observed_epoch == 0u);
    CHECK(recovery.completed_cycles == 1u);
    CHECK(recovery.last_cycle_tick == 100u);
    CHECK(!due(&recovery, 999u));
    CHECK(due(&recovery, 1000u));

    usb_storage_recovery_mark_cycle(&recovery, 1000u);
    CHECK(recovery.completed_cycles == 2u);
    CHECK(recovery.last_cycle_tick == 1000u);
    CHECK(!due(&recovery, 1899u));
    CHECK(due(&recovery, 1900u));
}

static void test_connect_disarms_recovery(void)
{
    puts("== accepted connect disarms recovery ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, false, 0u, 0u, 1u);
    usb_storage_recovery_observe(&recovery, true, 1u, 200u);

    CHECK(!recovery.armed);
    CHECK(recovery.session_connected);
    CHECK(recovery.observed_epoch == 1u);
    CHECK(recovery.completed_cycles == 0u);
    CHECK(!due(&recovery, UINT32_MAX));

    usb_storage_recovery_mark_cycle(&recovery, 300u);
    CHECK(recovery.completed_cycles == 0u);
}

static void test_owner_disconnect_rearms_full_fast_budget(void)
{
    puts("== owner disconnect rearms full fast-cycle budget ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, true, 1u, 50u, 0u);
    usb_storage_recovery_observe(&recovery, false, 2u, 1000u);

    CHECK(recovery.armed);
    CHECK(!recovery.session_connected);
    CHECK(recovery.observed_epoch == 2u);
    CHECK(recovery.completed_cycles == 0u);
    CHECK(recovery.last_cycle_tick == 1000u);
    CHECK(!due(&recovery, 1899u));
    CHECK(due(&recovery, 1900u));

    usb_storage_recovery_mark_cycle(&recovery, 1900u);
    CHECK(recovery.completed_cycles == 1u);
    CHECK(!usb_storage_recovery_uses_slow_cadence(
        &recovery, FAST_LIMIT));
}

static void test_same_disconnected_epoch_does_not_restart_deadline(void)
{
    puts("== polling the same disconnected epoch preserves deadline ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, true, 5u, 0u, 0u);
    usb_storage_recovery_observe(&recovery, false, 6u, 100u);
    usb_storage_recovery_observe(&recovery, false, 6u, 500u);
    usb_storage_recovery_observe(&recovery, false, 6u, 999u);

    CHECK(recovery.last_cycle_tick == 100u);
    CHECK(recovery.completed_cycles == 0u);
    CHECK(due(&recovery, 1000u));
}

static void test_epoch_change_catches_unobserved_connect_disconnect(void)
{
    puts("== epoch change catches connect-disconnect between polls ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, false, 0u, 10u, 7u);
    usb_storage_recovery_observe(&recovery, false, 2u, 400u);

    CHECK(recovery.armed);
    CHECK(recovery.observed_epoch == 2u);
    CHECK(recovery.completed_cycles == 0u);
    CHECK(recovery.last_cycle_tick == 400u);
    CHECK(!due(&recovery, 1299u));
    CHECK(due(&recovery, 1300u));
}

static void test_connected_snapshot_is_idempotent(void)
{
    puts("== repeated connected snapshot remains disarmed ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, true, 3u, 10u, 0u);
    usb_storage_recovery_observe(&recovery, true, 3u, 100u);
    usb_storage_recovery_observe(&recovery, true, 3u, 1000u);

    CHECK(!recovery.armed);
    CHECK(recovery.session_connected);
    CHECK(recovery.observed_epoch == 3u);
    CHECK(recovery.completed_cycles == 0u);
    CHECK(!due(&recovery, 50000u));
}

static void test_fast_cycles_transition_to_bounded_slow_cadence(void)
{
    puts("== fast cycles transition to bounded slow cadence ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, false, 0u, 0u, 0u);

    uint32_t now = 0u;
    for (uint32_t cycle = 0u; cycle < FAST_LIMIT; cycle++) {
        CHECK(!usb_storage_recovery_uses_slow_cadence(
            &recovery, FAST_LIMIT));
        CHECK(!due(&recovery, now + FAST_TICKS - 1u));
        now += FAST_TICKS;
        CHECK(due(&recovery, now));
        usb_storage_recovery_mark_cycle(&recovery, now);
        CHECK(recovery.completed_cycles == cycle + 1u);
    }

    CHECK(usb_storage_recovery_uses_slow_cadence(
        &recovery, FAST_LIMIT));
    CHECK(!due(&recovery, now + SLOW_TICKS - 1u));
    CHECK(due(&recovery, now + SLOW_TICKS));
}

static void test_reconnect_starts_a_fresh_recovery_epoch(void)
{
    puts("== reconnect starts a fresh recovery epoch ==");
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery, false, 0u, 0u, FAST_LIMIT);
    CHECK(usb_storage_recovery_uses_slow_cadence(
        &recovery, FAST_LIMIT));

    usb_storage_recovery_observe(&recovery, true, 1u, 100u);
    CHECK(!recovery.armed);
    usb_storage_recovery_observe(&recovery, false, 2u, 200u);

    CHECK(recovery.armed);
    CHECK(recovery.completed_cycles == 0u);
    CHECK(!usb_storage_recovery_uses_slow_cadence(
        &recovery, FAST_LIMIT));
    CHECK(!due(&recovery, 1099u));
    CHECK(due(&recovery, 1100u));
}

static void test_tick_wrap_is_elapsed_time_safe(void)
{
    puts("== tick wrap keeps elapsed-time comparison valid ==");
    usb_storage_recovery_t recovery;
    const uint32_t start = UINT32_MAX - 99u;
    usb_storage_recovery_init(&recovery, false, 0u, start, 0u);

    CHECK(!due(&recovery, 799u));
    CHECK(due(&recovery, 800u));
    usb_storage_recovery_mark_cycle(&recovery, 800u);
    CHECK(recovery.last_cycle_tick == 800u);
    CHECK(recovery.completed_cycles == 1u);
}

static void test_null_guards(void)
{
    puts("== null recovery guards ==");
    usb_storage_recovery_init(NULL, false, 0u, 0u, 0u);
    usb_storage_recovery_observe(NULL, false, 0u, 0u);
    usb_storage_recovery_mark_cycle(NULL, 0u);
    CHECK(!usb_storage_recovery_cycle_due(
        NULL, 0u, FAST_TICKS, FAST_LIMIT, SLOW_TICKS));
    CHECK(!usb_storage_recovery_uses_slow_cadence(NULL, FAST_LIMIT));
}

int main(void)
{
    test_cold_boot_recovery_continues_until_connect();
    test_connect_disarms_recovery();
    test_owner_disconnect_rearms_full_fast_budget();
    test_same_disconnected_epoch_does_not_restart_deadline();
    test_epoch_change_catches_unobserved_connect_disconnect();
    test_connected_snapshot_is_idempotent();
    test_fast_cycles_transition_to_bounded_slow_cadence();
    test_reconnect_starts_a_fresh_recovery_epoch();
    test_tick_wrap_is_elapsed_time_safe();
    test_null_guards();

    printf("TESTS_RUN=%u\n", s_checks);
    if (s_failures == 0) {
        puts("usb_storage_recovery tests passed");
        return 0;
    }
    printf("usb_storage_recovery tests FAILED (%d)\n", s_failures);
    return 1;
}
