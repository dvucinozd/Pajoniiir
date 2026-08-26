#include "s3_debug_auth.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void s3_debug_auth_init(s3_debug_auth_t *auth, uint32_t token, uint32_t now_ms)
{
    if (!auth) return;
    auth->token = token;
    auth->issued_ms = now_ms;
    auth->failures = 0u;
    auth->locked = 0u;
}

static bool parse_token(const char *candidate, uint32_t *out_token)
{
    if (!candidate || !out_token) return false;
    if (strlen(candidate) != S3_DEBUG_AUTH_TOKEN_DIGITS) return false;
    uint32_t token = 0u;
    uint32_t invalid = 0u;
    for (size_t i = 0u; i < S3_DEBUG_AUTH_TOKEN_DIGITS; i++) {
        unsigned char ch = (unsigned char)candidate[i];
        invalid |= (uint32_t)(ch < '0' || ch > '9');
        token = token * 10u + (uint32_t)(ch - '0');
    }
    if (invalid != 0u) return false;
    *out_token = token;
    return true;
}

s3_debug_auth_result_t s3_debug_auth_check(s3_debug_auth_t *auth,
                                           const char *candidate,
                                           uint32_t now_ms)
{
    if (!auth) return S3_DEBUG_AUTH_INVALID;
    if (auth->locked != 0u) return S3_DEBUG_AUTH_RATE_LIMITED;
    if ((uint32_t)(now_ms - auth->issued_ms) > S3_DEBUG_AUTH_TTL_MS) {
        return S3_DEBUG_AUTH_EXPIRED;
    }

    uint32_t parsed = 0u;
    bool well_formed = parse_token(candidate, &parsed);
    /* Compare every byte of the numeric value even for malformed input. The
     * HTTP-facing rate limit is the primary brute-force control; this avoids a
     * separate early equality path for valid six-digit candidates. */
    uint32_t difference = parsed ^ auth->token;
    difference |= difference >> 16;
    difference |= difference >> 8;
    if (well_formed && (difference & 0xFFu) == 0u) {
        auth->failures = 0u;
        return S3_DEBUG_AUTH_OK;
    }

    if (auth->failures < UINT8_MAX) auth->failures++;
    if (auth->failures >= S3_DEBUG_AUTH_MAX_FAILURES) {
        auth->locked = 1u;
        return S3_DEBUG_AUTH_RATE_LIMITED;
    }
    return S3_DEBUG_AUTH_INVALID;
}
