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
 * before the bundle parser is even entered.
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

/* `json` need not be NUL-terminated; `len` bounds it. */
p4_ota_pull_manifest_result_t p4_ota_pull_manifest_parse(
    const char *json, size_t len, p4_ota_pull_manifest_t *out);

const char *p4_ota_pull_manifest_result_name(p4_ota_pull_manifest_result_t r);

/* True when the advertised release differs from what is running, i.e. an
 * update is worth downloading. Comparison is exact: a build that reports a
 * different string is a different build, and downgrades are as legitimate as
 * upgrades on a bench. */
bool p4_ota_pull_manifest_differs(const p4_ota_pull_manifest_t *m,
                                  const char *running_version);
