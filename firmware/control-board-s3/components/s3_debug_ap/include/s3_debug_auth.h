#pragma once

#include <stdint.h>

#define S3_DEBUG_AUTH_TOKEN_DIGITS 6u
#define S3_DEBUG_AUTH_TOKEN_MIN 100000u
#define S3_DEBUG_AUTH_TOKEN_RANGE 900000u
#define S3_DEBUG_AUTH_TTL_MS (10u * 60u * 1000u)
#define S3_DEBUG_AUTH_MAX_FAILURES 5u

typedef enum {
    S3_DEBUG_AUTH_OK = 0,
    S3_DEBUG_AUTH_INVALID,
    S3_DEBUG_AUTH_EXPIRED,
    S3_DEBUG_AUTH_RATE_LIMITED,
} s3_debug_auth_result_t;

typedef struct {
    uint32_t token;
    uint32_t issued_ms;
    uint8_t failures;
    uint8_t locked;
} s3_debug_auth_t;

void s3_debug_auth_init(s3_debug_auth_t *auth, uint32_t token, uint32_t now_ms);
s3_debug_auth_result_t s3_debug_auth_check(s3_debug_auth_t *auth,
                                           const char *candidate,
                                           uint32_t now_ms);

