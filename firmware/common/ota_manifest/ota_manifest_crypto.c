#include "ota_manifest.h"

#include <string.h>

#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

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
    if (mbedtls_sha256(header, DDJ_OTA_SIGNED_SIZE, digest, 0) != 0) return false;

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int rc = mbedtls_pk_parse_public_key(
        &key, release_public_key_start,
        (size_t)(release_public_key_end - release_public_key_start));
    if (rc != 0 || !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA)) {
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
