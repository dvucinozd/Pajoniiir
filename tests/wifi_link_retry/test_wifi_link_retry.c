#include "wifi_link_retry.h"
#include "wifi_link_control.h"

#include <assert.h>
#include <stdio.h>

/* The bug this replaces: a failed start left active != desired, and the worker
 * loop retried immediately and forever. The property that matters is therefore
 * not "it backs off" but "it stops". */
static void test_the_budget_is_finite(void)
{
    wifi_link_retry_t r;
    wifi_link_retry_reset(&r);
    assert(!wifi_link_retry_exhausted(&r));

    assert(wifi_link_retry_note_failure(&r) == 1000u);
    assert(!wifi_link_retry_exhausted(&r));
    assert(wifi_link_retry_note_failure(&r) == 2000u);
    assert(!wifi_link_retry_exhausted(&r));
    assert(wifi_link_retry_note_failure(&r) == 0u);
    assert(wifi_link_retry_exhausted(&r));
    assert(wifi_link_retry_attempts(&r) == WIFI_LINK_RETRY_MAX_ATTEMPTS);
}

/* Calling on past a spent budget must keep returning "give up" and must not
 * wrap the counter back into a delay - that would restore the hot loop by
 * another route. */
static void test_calling_past_the_end_never_resumes_retrying(void)
{
    wifi_link_retry_t r;
    wifi_link_retry_reset(&r);
    for (int i = 0; i < 3; i++) (void)wifi_link_retry_note_failure(&r);
    for (int i = 0; i < 1000; i++) {
        assert(wifi_link_retry_note_failure(&r) == 0u);
        assert(wifi_link_retry_exhausted(&r));
        assert(wifi_link_retry_attempts(&r) == WIFI_LINK_RETRY_MAX_ATTEMPTS);
    }
}

/* A new operator request starts a fresh budget; that is the only way back. */
static void test_reset_restores_the_budget(void)
{
    wifi_link_retry_t r;
    wifi_link_retry_reset(&r);
    for (int i = 0; i < 5; i++) (void)wifi_link_retry_note_failure(&r);
    assert(wifi_link_retry_exhausted(&r));

    wifi_link_retry_reset(&r);
    assert(!wifi_link_retry_exhausted(&r));
    assert(wifi_link_retry_attempts(&r) == 0u);
    assert(wifi_link_retry_note_failure(&r) == 1000u);
}

/* Delays must grow, or three rapid attempts are barely better than one. */
static void test_backoff_increases(void)
{
    wifi_link_retry_t r;
    wifi_link_retry_reset(&r);
    uint32_t first = wifi_link_retry_note_failure(&r);
    uint32_t second = wifi_link_retry_note_failure(&r);
    assert(first > 0u);
    assert(second > first);
}

static void test_null_is_inert(void)
{
    wifi_link_retry_reset(NULL);
    assert(wifi_link_retry_note_failure(NULL) == 0u);
    assert(!wifi_link_retry_exhausted(NULL));
    assert(wifi_link_retry_attempts(NULL) == 0u);
}

static void test_control_policy_defers_changes_during_transition(void)
{
    assert(wifi_link_control_next(false, true, true) ==
           WIFI_LINK_CONTROL_WAIT_TRANSITION);
    assert(wifi_link_control_next(true, false, true) ==
           WIFI_LINK_CONTROL_WAIT_TRANSITION);

    /* A repeated request does not need to wait: no stack mutation follows. */
    assert(wifi_link_control_next(true, true, true) == WIFI_LINK_CONTROL_IDLE);
    assert(wifi_link_control_next(false, false, true) == WIFI_LINK_CONTROL_IDLE);
}

static void test_control_policy_selects_start_and_stop_when_safe(void)
{
    assert(wifi_link_control_next(true, false, false) == WIFI_LINK_CONTROL_START);
    assert(wifi_link_control_next(false, true, false) == WIFI_LINK_CONTROL_STOP);
    assert(wifi_link_control_next(true, true, false) == WIFI_LINK_CONTROL_IDLE);
    assert(wifi_link_control_next(false, false, false) == WIFI_LINK_CONTROL_IDLE);
}

int main(void)
{
    test_the_budget_is_finite();
    test_calling_past_the_end_never_resumes_retrying();
    test_reset_restores_the_budget();
    test_backoff_increases();
    test_null_is_inert();
    test_control_policy_defers_changes_during_transition();
    test_control_policy_selects_start_and_stop_when_safe();
    puts("wifi_link_retry tests passed");
    return 0;
}
