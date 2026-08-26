#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Channel metadata for pull OTA — the small document at
 * https://<host>/ota/latest.json that says which release exists and where its
 * bundle lives.
 *
 * This document is DISCOVERY, NOT AUTHENTICATION. It is fetched over HTTPS and
 * parsed here, but nothing in it is trusted: the .ddjota it points at carries
 * its own ECDSA-P256 manifest, and `ota_manifest` verifies that signature
 * before a single byte reaches the flash. A tampered latest.json can therefore
 * make the deck download the wrong file or no file, but it cannot make it run
 * unsigned firmware.
 *
 * `size` and `sha256` are here so a truncated or corrupted transfer is rejected
 * before the inactive slot is activated.
 *
 * Expected shape:
 *
 *   {
 *     "schema_version": 1,
 *     "release": "RC1-237-g7bf0fd3c",
 *     "p4": {
 *       "url": "RC1-237-g7bf0fd3c/main-deck-p4.ddjota",
 *       "size": 2147132,
 *       "sha256": "83ba98...e22"
 *     }
 *   }
 *
 * The parser is deliberately strict and allocation-free: fixed field bounds, no
 * nesting beyond what is shown, and every unexpected shape is a rejection
 * rather than a partially populated result.
 */

#define P4_OTA_PULL_RELEASE_MAX 48u
#define P4_OTA_PULL_URL_MAX     192u
#define P4_OTA_PULL_SHA256_HEX  64u

typedef enum {
    P4_OTA_PULL_MANIFEST_OK = 0,
    P4_OTA_PULL_MANIFEST_INVALID_ARG,
    P4_OTA_PULL_MANIFEST_MALFORMED,      /* not the document we expect */
    P4_OTA_PULL_MANIFEST_UNSUPPORTED,    /* schema_version we do not know */
    P4_OTA_PULL_MANIFEST_NO_TARGET,      /* well-formed, but carries no "p4" */
    P4_OTA_PULL_MANIFEST_FIELD_TOO_LONG,
    P4_OTA_PULL_MANIFEST_BAD_VALUE,      /* bad size, bad hex, empty string */
} p4_ota_pull_manifest_result_t;

typedef struct {
    char     release[P4_OTA_PULL_RELEASE_MAX + 1u];
    char     url[P4_OTA_PULL_URL_MAX + 1u];
    uint32_t size;
    uint8_t  sha256[32];
} p4_ota_pull_manifest_t;

typedef enum {
    P4_OTA_PULL_RELEASE_SAME = 0,
    P4_OTA_PULL_RELEASE_NEWER,
    P4_OTA_PULL_RELEASE_OLDER,
    P4_OTA_PULL_RELEASE_UNORDERED,
} p4_ota_pull_release_order_t;

typedef enum {
    P4_OTA_PULL_BUNDLE_RELEASE_OK = 0,
    P4_OTA_PULL_BUNDLE_RELEASE_INVALID_ARG,
    P4_OTA_PULL_BUNDLE_RELEASE_MISMATCH,
    P4_OTA_PULL_BUNDLE_RELEASE_NOT_NEWER,
} p4_ota_pull_bundle_release_result_t;

/* `json` need not be NUL-terminated; `len` bounds it. */
p4_ota_pull_manifest_result_t p4_ota_pull_manifest_parse(
    const char *json, size_t len, p4_ota_pull_manifest_t *out);

const char *p4_ota_pull_manifest_result_name(p4_ota_pull_manifest_result_t r);

/*
 * Compare Pajoniiir's `git describe` versions:
 *
 *   RC<tag>-<commits-since-tag>-g<hash>[-dirty]
 *
 * Pull OTA accepts only NEWER. A signed older bundle remains installable
 * through the local push-OTA service path, which keeps rollback possible
 * without allowing an unauthenticated channel document to force it.
 */
p4_ota_pull_release_order_t p4_ota_pull_release_compare(
    const char *offered_version, const char *running_version);

/* Convenience wrapper used by the pull worker. */
p4_ota_pull_release_order_t p4_ota_pull_manifest_order(
    const p4_ota_pull_manifest_t *m, const char *running_version);

/* Bind the authenticated bundle to the unauthenticated discovery offer and
 * repeat the newer-only decision over the signed version. */
p4_ota_pull_bundle_release_result_t p4_ota_pull_validate_bundle_release(
    const char *offered_version,
    const char *signed_version,
    const char *running_version);

/* Wrap-safe freshness predicate for an offer timestamp expressed in ticks. */
bool p4_ota_pull_offer_fresh(uint32_t now_ticks,
                             uint32_t offered_at_ticks,
                             uint32_t ttl_ticks);
