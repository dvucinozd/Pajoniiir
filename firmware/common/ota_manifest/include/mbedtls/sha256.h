#pragma once

/*
 * ESP-IDF 6.0 / Mbed TLS 4 removed the legacy mbedtls/sha256.h interface.
 * Pajoniiir's OTA paths only use the small SHA-256 subset below, so preserve
 * their existing state machines while backing the implementation with PSA
 * Crypto. This header intentionally lives in ota_manifest's public include
 * tree; every signed OTA producer already depends on that component.
 */

#include <stddef.h>
#include <stdint.h>

#include "psa/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mbed TLS 4 still has an internal typedef with this name, although the public
 * SHA-256 header was removed. A macro alias avoids redeclaring that typedef and
 * maps only the project source that includes this compatibility header. */
#define mbedtls_sha256_context psa_hash_operation_t

static inline void mbedtls_sha256_init(mbedtls_sha256_context *ctx)
{
    if (ctx) {
        *ctx = psa_hash_operation_init();
    }
}

static inline void mbedtls_sha256_free(mbedtls_sha256_context *ctx)
{
    if (ctx) {
        (void)psa_hash_abort(ctx);
        *ctx = psa_hash_operation_init();
    }
}

static inline int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{
    if (!ctx || is224 != 0) {
        return -1;
    }
    return psa_hash_setup(ctx, PSA_ALG_SHA_256) == PSA_SUCCESS ? 0 : -1;
}

static inline int mbedtls_sha256_update(mbedtls_sha256_context *ctx,
                                        const unsigned char *input,
                                        size_t input_len)
{
    if (!ctx || (!input && input_len != 0u)) {
        return -1;
    }
    return psa_hash_update(ctx, input, input_len) == PSA_SUCCESS ? 0 : -1;
}

static inline int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,
                                        unsigned char output[32])
{
    if (!ctx || !output) {
        return -1;
    }
    size_t output_len = 0u;
    psa_status_t status = psa_hash_finish(ctx, output, 32u, &output_len);
    return status == PSA_SUCCESS && output_len == 32u ? 0 : -1;
}

static inline int mbedtls_sha256(const unsigned char *input,
                                 size_t input_len,
                                 unsigned char output[32],
                                 int is224)
{
    if ((!input && input_len != 0u) || !output || is224 != 0) {
        return -1;
    }
    size_t output_len = 0u;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256,
                                           input,
                                           input_len,
                                           output,
                                           32u,
                                           &output_len);
    return status == PSA_SUCCESS && output_len == 32u ? 0 : -1;
}

#ifdef __cplusplus
}
#endif
