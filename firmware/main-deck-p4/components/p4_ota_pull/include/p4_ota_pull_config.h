#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Validation for the pull-OTA service-network configuration.
 *
 * Split out from storage and from the web handler because these are the rules
 * that fail *silently* when they are wrong: a 5-character password is accepted
 * by every layer above and then simply never associates, which on a deck with
 * no serial console is indistinguishable from a firmware fault. Rejecting it at
 * entry, with a reason, is the difference between a typo and an evening.
 *
 * Nothing here stores or logs a credential; callers must not either.
 */

#define P4_OTA_CFG_SSID_MAX 32u    /* 802.11 SSID is 32 octets */
#define P4_OTA_CFG_PASS_MIN 8u     /* WPA2-PSK passphrase bounds */
#define P4_OTA_CFG_PASS_MAX 63u
#define P4_OTA_CFG_PSK_HEX  64u    /* or a raw 256-bit PSK in hex */
#define P4_OTA_CFG_URL_MAX  160u

typedef enum {
    P4_OTA_CFG_OK = 0,
    P4_OTA_CFG_EMPTY,
    P4_OTA_CFG_TOO_LONG,
    P4_OTA_CFG_TOO_SHORT,
    P4_OTA_CFG_BAD_CHARS,
    P4_OTA_CFG_NOT_HTTPS,
    P4_OTA_CFG_URL_HAS_CREDENTIALS,
} p4_ota_cfg_result_t;

const char *p4_ota_cfg_result_name(p4_ota_cfg_result_t r);

/* 1..32 octets, no control characters. */
p4_ota_cfg_result_t p4_ota_cfg_check_ssid(const char *ssid);

/* WPA2-PSK: 8..63 printable ASCII, or exactly 64 hex digits for a raw PSK.
 * An open network is expressed by clearing the password, not by passing one
 * that is too short. */
p4_ota_cfg_result_t p4_ota_cfg_check_password(const char *password);

/* Must be https://: the update transport carries no authenticity of its own —
 * that stays with the bundle signature — but plaintext would leak which deck
 * pulls which build, and lets anyone on the path stall or misdirect updates.
 *
 * Credentials embedded in the authority (user:pass@host) are refused outright;
 * they end up in logs and status output, which is exactly what must not happen.
 */
p4_ota_cfg_result_t p4_ota_cfg_check_url(const char *url);
