#include "p4_ota_pull_config.h"

#include <string.h>

static bool is_printable_ascii(char c)
{
    return (unsigned char)c >= 0x20u && (unsigned char)c < 0x7Fu;
}

static bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

const char *p4_ota_cfg_result_name(p4_ota_cfg_result_t r)
{
    switch (r) {
    case P4_OTA_CFG_OK:                    return "ok";
    case P4_OTA_CFG_EMPTY:                 return "empty";
    case P4_OTA_CFG_TOO_LONG:              return "too-long";
    case P4_OTA_CFG_TOO_SHORT:             return "too-short";
    case P4_OTA_CFG_BAD_CHARS:             return "bad-characters";
    case P4_OTA_CFG_NOT_HTTPS:             return "not-https";
    case P4_OTA_CFG_URL_HAS_CREDENTIALS:   return "url-has-credentials";
    }
    return "unknown";
}

p4_ota_cfg_result_t p4_ota_cfg_check_ssid(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return P4_OTA_CFG_EMPTY;
    size_t n = strnlen(ssid, P4_OTA_CFG_SSID_MAX + 1u);
    if (n > P4_OTA_CFG_SSID_MAX) return P4_OTA_CFG_TOO_LONG;
    for (size_t i = 0; i < n; i++) {
        /* Control characters only. An SSID may legitimately contain UTF-8, so
         * high bytes are left alone rather than forced to ASCII. */
        if ((unsigned char)ssid[i] < 0x20u || (unsigned char)ssid[i] == 0x7Fu) {
            return P4_OTA_CFG_BAD_CHARS;
        }
    }
    return P4_OTA_CFG_OK;
}

p4_ota_cfg_result_t p4_ota_cfg_check_password(const char *password)
{
    if (!password || password[0] == '\0') return P4_OTA_CFG_EMPTY;
    size_t n = strnlen(password, P4_OTA_CFG_PSK_HEX + 1u);
    if (n > P4_OTA_CFG_PSK_HEX) return P4_OTA_CFG_TOO_LONG;

    if (n == P4_OTA_CFG_PSK_HEX) {
        for (size_t i = 0; i < n; i++) {
            if (!is_hex_digit(password[i])) return P4_OTA_CFG_BAD_CHARS;
        }
        return P4_OTA_CFG_OK;   /* raw PSK */
    }

    if (n < P4_OTA_CFG_PASS_MIN) return P4_OTA_CFG_TOO_SHORT;
    if (n > P4_OTA_CFG_PASS_MAX) return P4_OTA_CFG_TOO_LONG;
    for (size_t i = 0; i < n; i++) {
        if (!is_printable_ascii(password[i])) return P4_OTA_CFG_BAD_CHARS;
    }
    return P4_OTA_CFG_OK;
}

p4_ota_cfg_result_t p4_ota_cfg_check_url(const char *url)
{
    static const char scheme[] = "https://";
    const size_t scheme_len = sizeof(scheme) - 1u;

    if (!url || url[0] == '\0') return P4_OTA_CFG_EMPTY;
    size_t n = strnlen(url, P4_OTA_CFG_URL_MAX + 1u);
    if (n > P4_OTA_CFG_URL_MAX) return P4_OTA_CFG_TOO_LONG;
    for (size_t i = 0; i < n; i++) {
        if (!is_printable_ascii(url[i]) || url[i] == ' ') return P4_OTA_CFG_BAD_CHARS;
    }
    if (strncmp(url, scheme, scheme_len) != 0) return P4_OTA_CFG_NOT_HTTPS;
    if (n == scheme_len) return P4_OTA_CFG_EMPTY;   /* scheme with no host */

    /* Reject user:pass@host. The authority ends at the first '/', '?' or '#';
     * an '@' before that carries credentials that would end up in logs and in
     * status output. */
    const char *authority_end = url + n;
    for (const char *p = url + scheme_len; p < url + n; p++) {
        if (*p == '/' || *p == '?' || *p == '#') { authority_end = p; break; }
    }
    for (const char *p = url + scheme_len; p < authority_end; p++) {
        if (*p == '@') return P4_OTA_CFG_URL_HAS_CREDENTIALS;
    }
    if (authority_end == url + scheme_len) return P4_OTA_CFG_EMPTY;   /* no host */

    return P4_OTA_CFG_OK;
}
