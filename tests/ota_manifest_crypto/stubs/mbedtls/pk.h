#pragma once

#include <stddef.h>
#include <stdint.h>
#include "psa/crypto.h"

typedef struct { int initialized; } mbedtls_pk_context;

#define MBEDTLS_MD_SHA256 4
#define MBEDTLS_PK_ALG_ECDSA(alg) ((psa_algorithm_t)((alg) | 0x10000000u))

void mbedtls_pk_init(mbedtls_pk_context *ctx);
void mbedtls_pk_free(mbedtls_pk_context *ctx);
int mbedtls_pk_parse_public_key(mbedtls_pk_context *ctx,
                                const uint8_t *key, size_t key_length);
int mbedtls_pk_can_do_psa(const mbedtls_pk_context *ctx,
                          psa_algorithm_t algorithm,
                          psa_key_usage_t usage);
int mbedtls_pk_verify(mbedtls_pk_context *ctx, int md_alg,
                      const uint8_t *hash, size_t hash_len,
                      const uint8_t *signature, size_t signature_len);
