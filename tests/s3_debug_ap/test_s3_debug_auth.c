#include "s3_debug_auth.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_exact_token_is_required(void)
{
    s3_debug_auth_t auth;
    s3_debug_auth_init(&auth, 123456u, 1000u);
    assert(s3_debug_auth_check(&auth, NULL, 1000u) == S3_DEBUG_AUTH_INVALID);
    assert(s3_debug_auth_check(&auth, "12345", 1000u) == S3_DEBUG_AUTH_INVALID);
    assert(s3_debug_auth_check(&auth, "12345x", 1000u) == S3_DEBUG_AUTH_INVALID);
    assert(s3_debug_auth_check(&auth, "1234567", 1000u) == S3_DEBUG_AUTH_INVALID);
    s3_debug_auth_init(&auth, 123456u, 1000u);
    assert(s3_debug_auth_check(&auth, "123456", 1000u) == S3_DEBUG_AUTH_OK);
    assert(auth.failures == 0u);
}

static void test_failure_budget_locks_until_reinitialised(void)
{
    s3_debug_auth_t auth;
    s3_debug_auth_init(&auth, 654321u, 0u);
    for (uint8_t i = 1u; i < S3_DEBUG_AUTH_MAX_FAILURES; i++) {
        assert(s3_debug_auth_check(&auth, "000000", i) == S3_DEBUG_AUTH_INVALID);
    }
    assert(s3_debug_auth_check(&auth, "000000", 5u) ==
           S3_DEBUG_AUTH_RATE_LIMITED);
    assert(s3_debug_auth_check(&auth, "654321", 6u) ==
           S3_DEBUG_AUTH_RATE_LIMITED);
    s3_debug_auth_init(&auth, 654321u, 7u);
    assert(s3_debug_auth_check(&auth, "654321", 7u) == S3_DEBUG_AUTH_OK);
}

static void test_expiry_and_tick_wrap_are_bounded(void)
{
    s3_debug_auth_t auth;
    s3_debug_auth_init(&auth, 111111u, UINT32_MAX - 100u);
    assert(s3_debug_auth_check(&auth, "111111", 50u) == S3_DEBUG_AUTH_OK);
    assert(s3_debug_auth_check(&auth, "111111",
                               (UINT32_MAX - 100u) + S3_DEBUG_AUTH_TTL_MS) ==
           S3_DEBUG_AUTH_OK);
    assert(s3_debug_auth_check(&auth, "111111",
                               (UINT32_MAX - 100u) + S3_DEBUG_AUTH_TTL_MS + 1u) ==
           S3_DEBUG_AUTH_EXPIRED);
}

int main(void)
{
    test_exact_token_is_required();
    test_failure_budget_locks_until_reinitialised();
    test_expiry_and_tick_wrap_are_bounded();
    puts("s3_debug_auth tests passed");
    return 0;
}
