#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ota_manifest.h"
#include "mbedtls/pk.h"

const uint8_t fake_release_key_start[4]
    __asm__("_binary_ddj_ota_release_public_der_start") = {1u, 2u, 3u, 4u};
const uint8_t fake_release_key_end[1]
    __asm__("_binary_ddj_ota_release_public_der_end") = {0u};

static psa_status_t s_hash_result;
static size_t s_hash_size;
static int s_parse_result;
static int s_key_compatible;
static int s_verify_result;
static unsigned s_hash_calls;
static unsigned s_parse_calls;
static unsigned s_verify_calls;
static uint8_t s_der[72];
static size_t s_der_size;

static void reset_fakes(void)
{
    s_hash_result = PSA_SUCCESS;
    s_hash_size = DDJ_OTA_SHA256_SIZE;
    s_parse_result = 0;
    s_key_compatible = 1;
    s_verify_result = 0;
    s_hash_calls = s_parse_calls = s_verify_calls = 0u;
    memset(s_der, 0, sizeof(s_der));
    s_der_size = 0u;
}

psa_status_t psa_hash_compute(psa_algorithm_t algorithm,
                              const uint8_t *input, size_t input_length,
                              uint8_t *hash, size_t hash_size,
                              size_t *hash_length)
{
    assert(algorithm == PSA_ALG_SHA_256);
    assert(input != NULL && input_length == DDJ_OTA_SIGNED_SIZE);
    assert(hash_size == DDJ_OTA_SHA256_SIZE);
    s_hash_calls++;
    memset(hash, 0xa5, hash_size);
    *hash_length = s_hash_size;
    return s_hash_result;
}

void mbedtls_pk_init(mbedtls_pk_context *ctx) { ctx->initialized = 1; }
void mbedtls_pk_free(mbedtls_pk_context *ctx) { ctx->initialized = 0; }
int mbedtls_pk_parse_public_key(mbedtls_pk_context *ctx,
                                const uint8_t *key, size_t key_length)
{
    assert(ctx->initialized == 1 && key != NULL);
    (void)key_length;
    s_parse_calls++;
    return s_parse_result;
}
int mbedtls_pk_can_do_psa(const mbedtls_pk_context *ctx,
                          psa_algorithm_t algorithm,
                          psa_key_usage_t usage)
{
    assert(ctx->initialized == 1);
    assert(algorithm == MBEDTLS_PK_ALG_ECDSA(PSA_ALG_SHA_256));
    assert(usage == PSA_KEY_USAGE_VERIFY_HASH);
    return s_key_compatible;
}
int mbedtls_pk_verify(mbedtls_pk_context *ctx, int md_alg,
                      const uint8_t *hash, size_t hash_len,
                      const uint8_t *signature, size_t signature_len)
{
    assert(ctx->initialized == 1 && md_alg == MBEDTLS_MD_SHA256);
    assert(hash != NULL && hash_len == DDJ_OTA_SHA256_SIZE);
    assert(signature_len <= sizeof(s_der));
    memcpy(s_der, signature, signature_len);
    s_der_size = signature_len;
    s_verify_calls++;
    return s_verify_result;
}

static void test_rejects_invalid_inputs_and_crypto_failures(void)
{
    uint8_t header[DDJ_OTA_HEADER_SIZE] = {0};
    reset_fakes();
    assert(!ddj_ota_manifest_verify_signature(NULL, sizeof(header)));
    assert(!ddj_ota_manifest_verify_signature(header, DDJ_OTA_HEADER_SIZE - 1u));
    assert(s_hash_calls == 0u);

    s_hash_result = PSA_ERROR_GENERIC_ERROR;
    assert(!ddj_ota_manifest_verify_signature(header, sizeof(header)));
    assert(s_parse_calls == 0u);

    reset_fakes();
    s_hash_size = DDJ_OTA_SHA256_SIZE - 1u;
    assert(!ddj_ota_manifest_verify_signature(header, sizeof(header)));
    assert(s_parse_calls == 0u);

    reset_fakes();
    s_parse_result = -1;
    assert(!ddj_ota_manifest_verify_signature(header, sizeof(header)));
    assert(s_verify_calls == 0u);

    reset_fakes();
    s_key_compatible = 0;
    assert(!ddj_ota_manifest_verify_signature(header, sizeof(header)));
    assert(s_verify_calls == 0u);

    reset_fakes();
    s_verify_result = -1;
    assert(!ddj_ota_manifest_verify_signature(header, sizeof(header)));
    assert(s_verify_calls == 1u);
}

static void test_raw_signature_is_canonical_der(void)
{
    uint8_t header[DDJ_OTA_HEADER_SIZE] = {0};
    uint8_t *raw = header + DDJ_OTA_OFFSET_SIGNATURE;
    /* r needs a leading zero; s has 30 redundant leading zero bytes. */
    raw[0] = 0x80u;
    raw[31] = 0x01u;
    raw[62] = 0x7fu;
    raw[63] = 0x55u;

    reset_fakes();
    assert(ddj_ota_manifest_verify_signature(header, sizeof(header)));
    assert(s_hash_calls == 1u && s_parse_calls == 1u && s_verify_calls == 1u);
    assert(s_der_size == 41u);
    assert(s_der[0] == 0x30u && s_der[1] == 39u);
    assert(s_der[2] == 0x02u && s_der[3] == 33u && s_der[4] == 0u);
    assert(s_der[5] == 0x80u && s_der[36] == 0x01u);
    assert(s_der[37] == 0x02u && s_der[38] == 2u);
    assert(s_der[39] == 0x7fu && s_der[40] == 0x55u);
}

int main(void)
{
    test_rejects_invalid_inputs_and_crypto_failures();
    test_raw_signature_is_canonical_der();
    puts("OTA manifest production crypto tests passed");
    return 0;
}
