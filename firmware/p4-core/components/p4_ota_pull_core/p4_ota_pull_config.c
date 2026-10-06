#include "p4_ota_pull_config.h"

#include <string.h>

static size_t bounded_strlen(const char *s, size_t limit)
{
    size_t n = 0u;
    while (n < limit && s[n] != '\0') {
        n++;
    }
    return n;
}

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
    size_t n = bounded_strlen(ssid, P4_OTA_CFG_SSID_MAX + 1u);
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
    size_t n = bounded_strlen(password, P4_OTA_CFG_PSK_HEX + 1u);
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
    size_t n = bounded_strlen(url, P4_OTA_CFG_URL_MAX + 1u);
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

/* Locate `"key" :` and return the offset of the value, or SIZE_MAX. */
static size_t value_offset(const char *json, size_t len, const char *key)
{
    size_t klen = strlen(key);
    if (!json || len < klen + 3u) return (size_t)-1;
    for (size_t i = 0; i + klen + 2u <= len; i++) {
        if (json[i] != 0x22) continue;
        if (memcmp(json + i + 1u, key, klen) != 0) continue;
        if (json[i + 1u + klen] != 0x22) continue;
        size_t j = i + 2u + klen;
        while (j < len && (json[j] == 0x20 || json[j] == 0x09 ||
                           json[j] == 0x0D || json[j] == 0x0A)) j++;
        if (j >= len || json[j] != 0x3A) continue;
        j++;
        while (j < len && (json[j] == 0x20 || json[j] == 0x09 ||
                           json[j] == 0x0D || json[j] == 0x0A)) j++;
        return j;
    }
    return (size_t)-1;
}

bool p4_ota_cfg_extract_string(const char *json, size_t len, const char *key,
                               char *out, size_t cap)
{
    if (!out || cap == 0u) return false;
    out[0] = 0;
    size_t j = value_offset(json, len, key);
    if (j == (size_t)-1 || j >= len || json[j] != 0x22) return false;
    j++;
    size_t n = 0;
    while (j < len && json[j] != 0x22) {
        if (json[j] == 0x5C) { out[0] = 0; return false; }   /* no escapes */
        if (n + 1u >= cap) { out[0] = 0; return false; }     /* refuse, do not truncate */
        out[n++] = json[j++];
    }
    if (j >= len) { out[0] = 0; return false; }              /* unterminated */
    out[n] = 0;
    return true;
}

bool p4_ota_cfg_extract_true(const char *json, size_t len, const char *key)
{
    size_t j = value_offset(json, len, key);
    if (j == (size_t)-1) return false;
    return (len - j) >= 4u && memcmp(json + j, "true", 4u) == 0;
}
