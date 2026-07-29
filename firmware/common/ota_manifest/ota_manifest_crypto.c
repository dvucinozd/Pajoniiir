#include "ota_manifest.h"

#include <string.h>

#include "mbedtls/pk.h"
#include "psa/crypto.h"

extern const uint8_t release_public_key_start[]
    asm("_binary_ddj_ota_release_public_der_start");
extern const uint8_t release_public_key_end[]
    asm("_binary_ddj_ota_release_public_der_end");

static size_t write_der_integer(uint8_t *dst, const uint8_t raw[32])
{
    size_t first = 0;
    while (first < 31u && raw[first] == 0) ++first;
    size_t value_size = 32u - first;
    bool leading_zero = (raw[first] & 0x80u) != 0;
    dst[0] = 0x02u;
    dst[1] = (uint8_t)(value_size + (leading_zero ? 1u : 0u));
    size_t offset = 2u;
    if (leading_zero) dst[offset++] = 0;
    memcpy(dst + offset, raw + first, value_size);
    return offset + value_size;
}

static size_t raw_signature_to_der(const uint8_t raw[DDJ_OTA_SIGNATURE_SIZE],
                                   uint8_t der[72])
{
    uint8_t integers[70];
    size_t r_size = write_der_integer(integers, raw);
    size_t s_size = write_der_integer(integers + r_size, raw + 32u);
    der[0] = 0x30u;
    der[1] = (uint8_t)(r_size + s_size);
    memcpy(der + 2u, integers, r_size + s_size);
    return 2u + r_size + s_size;
}

bool ddj_ota_manifest_verify_signature(const uint8_t *header, size_t header_size)
{
    if (!header || header_size < DDJ_OTA_HEADER_SIZE) return false;

    uint8_t digest[DDJ_OTA_SHA256_SIZE];
    size_t digest_size = 0u;
    if (psa_hash_compute(PSA_ALG_SHA_256,
                         header,
                         DDJ_OTA_SIGNED_SIZE,
                         digest,
                         sizeof(digest),
                         &digest_size) != PSA_SUCCESS ||
        digest_size != sizeof(digest)) {
        return false;
    }

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int rc = mbedtls_pk_parse_public_key(
        &key, release_public_key_start,
        (size_t)(release_public_key_end - release_public_key_start));
    /* Check the key type, not just that it parsed. The signature bytes are
     * converted below into DER as a fixed-size ECDSA (r,s) pair, so a key that
     * is not ECDSA would have that layout imposed on it. The committed key makes
     * this unreachable today, which is exactly why it is worth keeping: it
     * catches a key swapped for the wrong type instead of trusting the build.
     *
     * ESP-IDF 6.0.2 moved mbedTLS to the TF-PSA-Crypto layer, where
     * mbedtls_pk_can_do(pk, MBEDTLS_PK_ECDSA) no longer exists. The equivalent
     * is mbedtls_pk_can_do_psa() with the PSA algorithm and the usage the key is
     * about to be put to - here, verifying a SHA-256 hash. */
    if (rc != 0 ||
        !mbedtls_pk_can_do_psa(&key,
                               MBEDTLS_PK_ALG_ECDSA(PSA_ALG_SHA_256),
                               PSA_KEY_USAGE_VERIFY_HASH)) {
        mbedtls_pk_free(&key);
        return false;
    }

    uint8_t der_signature[72];
    size_t der_size = raw_signature_to_der(
        header + DDJ_OTA_OFFSET_SIGNATURE, der_signature);
    rc = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                           der_signature, der_size);
    mbedtls_pk_free(&key);
    return rc == 0;
}
