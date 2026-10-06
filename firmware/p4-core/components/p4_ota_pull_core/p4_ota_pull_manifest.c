#include "p4_ota_pull_manifest.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

/*
 * A hand-written extractor rather than a JSON library. The document is three
 * fields inside one object and arrives from the network, so the properties that
 * matter are "cannot over-read", "cannot allocate" and "rejects anything it
 * does not fully understand" - not generality.
 */

#define SCHEMA_VERSION_SUPPORTED 1

typedef struct {
    const char *p;
    const char *end;
} cur_t;

static bool hex_nibble(char ch, uint8_t *out);

static void skip_ws(cur_t *c)
{
    while (c->p < c->end &&
           (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n')) {
        c->p++;
    }
}

#define JSON_MAX_DEPTH 8u

static bool json_skip_value(cur_t *c, unsigned depth);

static bool json_skip_string(cur_t *c)
{
    if (c->p >= c->end || *c->p != '"') return false;
    c->p++;
    while (c->p < c->end) {
        const unsigned char ch = (unsigned char)*c->p++;
        if (ch == '"') return true;
        if (ch < 0x20u) return false;
        if (ch != '\\') continue;
        if (c->p >= c->end) return false;
        const char esc = *c->p++;
        if (strchr("\"\\/bfnrt", esc)) continue;
        if (esc != 'u' || (size_t)(c->end - c->p) < 4u) return false;
        for (unsigned i = 0u; i < 4u; i++) {
            uint8_t nibble;
            if (!hex_nibble(c->p[i], &nibble)) return false;
        }
        c->p += 4u;
    }
    return false;
}

static bool json_skip_number(cur_t *c)
{
    const char *start = c->p;
    if (c->p < c->end && *c->p == '-') c->p++;
    if (c->p >= c->end) return false;
    if (*c->p == '0') {
        c->p++;
        if (c->p < c->end && isdigit((unsigned char)*c->p)) return false;
    } else {
        if (*c->p < '1' || *c->p > '9') return false;
        while (c->p < c->end && isdigit((unsigned char)*c->p)) c->p++;
    }
    if (c->p < c->end && *c->p == '.') {
        c->p++;
        if (c->p >= c->end || !isdigit((unsigned char)*c->p)) return false;
        while (c->p < c->end && isdigit((unsigned char)*c->p)) c->p++;
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        c->p++;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-')) c->p++;
        if (c->p >= c->end || !isdigit((unsigned char)*c->p)) return false;
        while (c->p < c->end && isdigit((unsigned char)*c->p)) c->p++;
    }
    return c->p > start;
}

static bool json_skip_compound(cur_t *c, char open, char close, unsigned depth)
{
    if (depth >= JSON_MAX_DEPTH || c->p >= c->end || *c->p != open) return false;
    c->p++;
    skip_ws(c);
    if (c->p < c->end && *c->p == close) { c->p++; return true; }
    for (;;) {
        if (open == '{') {
            if (!json_skip_string(c)) return false;
            skip_ws(c);
            if (c->p >= c->end || *c->p++ != ':') return false;
            skip_ws(c);
        }
        if (!json_skip_value(c, depth + 1u)) return false;
        skip_ws(c);
        if (c->p < c->end && *c->p == close) { c->p++; return true; }
        if (c->p >= c->end || *c->p++ != ',') return false;
        skip_ws(c);
    }
}

static bool json_skip_value(cur_t *c, unsigned depth)
{
    skip_ws(c);
    if (c->p >= c->end) return false;
    if (*c->p == '"') return json_skip_string(c);
    if (*c->p == '{') return json_skip_compound(c, '{', '}', depth);
    if (*c->p == '[') return json_skip_compound(c, '[', ']', depth);
    if (*c->p == '-' || isdigit((unsigned char)*c->p)) return json_skip_number(c);
    static const char *const literals[] = { "true", "false", "null" };
    for (size_t i = 0u; i < sizeof(literals) / sizeof(literals[0]); i++) {
        const size_t n = strlen(literals[i]);
        if ((size_t)(c->end - c->p) >= n && memcmp(c->p, literals[i], n) == 0) {
            c->p += n;
            return true;
        }
    }
    return false;
}

static bool json_document_valid(const char *json, size_t len)
{
    cur_t c = { json, json + len };
    skip_ws(&c);
    if (c.p >= c.end || *c.p != '{' || !json_skip_value(&c, 0u)) return false;
    skip_ws(&c);
    return c.p == c.end;
}

static unsigned key_occurrences(const char *json, size_t len, const char *key)
{
    unsigned count = 0u;
    const size_t klen = strlen(key);
    for (size_t i = 0u; i + klen + 2u <= len; i++) {
        if (json[i] == '"' && memcmp(json + i + 1u, key, klen) == 0 &&
            json[i + klen + 1u] == '"') {
            cur_t c = { json + i + klen + 2u, json + len };
            skip_ws(&c);
            if (c.p < c.end && *c.p == ':') count++;
        }
    }
    return count;
}

/* Find `"key"` at any depth inside the bounded buffer, returning a cursor just
 * past the following colon. Depth-blind on purpose: the document has one nested
 * object and duplicated keys are rejected by the callers' expectations rather
 * than by a full parser. */
static bool seek_key(const char *json, size_t len, const char *key, cur_t *out)
{
    size_t klen = strlen(key);
    if (len < klen + 3u) return false;
    for (size_t i = 0; i + klen + 2u <= len; i++) {
        if (json[i] != '"') continue;
        if (memcmp(json + i + 1u, key, klen) != 0) continue;
        if (json[i + 1u + klen] != '"') continue;
        cur_t c = { json + i + 2u + klen, json + len };
        skip_ws(&c);
        if (c.p >= c.end || *c.p != ':') continue;
        c.p++;
        skip_ws(&c);
        *out = c;
        return true;
    }
    return false;
}

/* Copy a JSON string value into a fixed buffer. Rejects escapes outright: no
 * field in this document legitimately contains one, and accepting them would
 * mean implementing unescaping on network input for no benefit. */
static p4_ota_pull_manifest_result_t copy_string(cur_t c, char *dst, size_t cap)
{
    if (c.p >= c.end || *c.p != '"') return P4_OTA_PULL_MANIFEST_MALFORMED;
    c.p++;
    size_t n = 0;
    while (c.p < c.end && *c.p != '"') {
        if (*c.p == '\\') return P4_OTA_PULL_MANIFEST_BAD_VALUE;
        if ((unsigned char)*c.p < 0x20u) return P4_OTA_PULL_MANIFEST_BAD_VALUE;
        if (n >= cap) return P4_OTA_PULL_MANIFEST_FIELD_TOO_LONG;
        dst[n++] = *c.p++;
    }
    if (c.p >= c.end) return P4_OTA_PULL_MANIFEST_MALFORMED;   /* unterminated */
    if (n == 0u) return P4_OTA_PULL_MANIFEST_BAD_VALUE;
    dst[n] = '\0';
    return P4_OTA_PULL_MANIFEST_OK;
}

static p4_ota_pull_manifest_result_t read_u32(cur_t c, uint32_t *out)
{
    if (c.p >= c.end || *c.p < '0' || *c.p > '9') {
        return P4_OTA_PULL_MANIFEST_MALFORMED;
    }
    uint64_t v = 0;
    while (c.p < c.end && *c.p >= '0' && *c.p <= '9') {
        v = v * 10u + (uint64_t)(*c.p - '0');
        if (v > 0xFFFFFFFFull) return P4_OTA_PULL_MANIFEST_BAD_VALUE;
        c.p++;
    }
    if (c.p < c.end && !isspace((unsigned char)*c.p) && *c.p != ',' && *c.p != '}') {
        return P4_OTA_PULL_MANIFEST_MALFORMED;
    }
    if (v == 0u) return P4_OTA_PULL_MANIFEST_BAD_VALUE;
    *out = (uint32_t)v;
    return P4_OTA_PULL_MANIFEST_OK;
}

static bool hex_nibble(char ch, uint8_t *out)
{
    if (ch >= '0' && ch <= '9') { *out = (uint8_t)(ch - '0'); return true; }
    if (ch >= 'a' && ch <= 'f') { *out = (uint8_t)(ch - 'a' + 10); return true; }
    if (ch >= 'A' && ch <= 'F') { *out = (uint8_t)(ch - 'A' + 10); return true; }
    return false;
}

static p4_ota_pull_manifest_result_t read_sha256(cur_t c, uint8_t out[32])
{
    char hex[P4_OTA_PULL_SHA256_HEX + 1u];
    p4_ota_pull_manifest_result_t rc = copy_string(c, hex, P4_OTA_PULL_SHA256_HEX);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;
    if (strlen(hex) != P4_OTA_PULL_SHA256_HEX) return P4_OTA_PULL_MANIFEST_BAD_VALUE;
    for (size_t i = 0; i < 32u; i++) {
        uint8_t hi, lo;
        if (!hex_nibble(hex[i * 2u], &hi) || !hex_nibble(hex[i * 2u + 1u], &lo)) {
            return P4_OTA_PULL_MANIFEST_BAD_VALUE;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return P4_OTA_PULL_MANIFEST_OK;
}

static bool relative_bundle_url_valid(const char *url)
{
    if (!url || url[0] == '\0' || url[0] == '/' || url[0] == '\\') return false;
    if (strstr(url, "://") || strchr(url, '?') || strchr(url, '#') ||
        strchr(url, '\\') || strchr(url, '%') || strchr(url, ':')) {
        return false;
    }
    const char *segment = url;
    for (const char *p = url; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - segment);
            if (len == 0u ||
                (len == 1u && segment[0] == '.') ||
                (len == 2u && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if (*p == '\0') break;
            segment = p + 1;
        } else if ((unsigned char)*p < 0x21u ||
                   (unsigned char)*p > 0x7Eu) {
            return false;
        }
    }
    return true;
}

p4_ota_pull_manifest_result_t p4_ota_pull_manifest_parse(
    const char *json, size_t len, p4_ota_pull_manifest_t *out)
{
    if (!json || !out || len == 0u) return P4_OTA_PULL_MANIFEST_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!json_document_valid(json, len) ||
        key_occurrences(json, len, "schema_version") != 1u ||
        key_occurrences(json, len, "release") != 1u ||
        key_occurrences(json, len, "p4") > 1u) {
        return P4_OTA_PULL_MANIFEST_MALFORMED;
    }

    cur_t c;
    if (!seek_key(json, len, "schema_version", &c)) {
        return P4_OTA_PULL_MANIFEST_MALFORMED;
    }
    uint32_t schema = 0;
    p4_ota_pull_manifest_result_t rc = read_u32(c, &schema);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;
    if (schema != SCHEMA_VERSION_SUPPORTED) return P4_OTA_PULL_MANIFEST_UNSUPPORTED;

    if (!seek_key(json, len, "release", &c)) return P4_OTA_PULL_MANIFEST_MALFORMED;
    rc = copy_string(c, out->release, P4_OTA_PULL_RELEASE_MAX);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;

    /* The "p4" object must exist; a well-formed document that only carries
     * other targets is not an error, it simply has nothing for this board. */
    cur_t target;
    if (!seek_key(json, len, "p4", &target)) return P4_OTA_PULL_MANIFEST_NO_TARGET;
    if (target.p >= target.end || *target.p != '{') {
        return P4_OTA_PULL_MANIFEST_MALFORMED;
    }
    /* Bound the remaining lookups to the target object so a stray "url" that
     * belongs to another target cannot be picked up. */
    const char *obj = target.p;
    size_t obj_len = 0;
    int depth = 0;
    for (const char *q = obj; q < target.end; q++) {
        if (*q == '{') depth++;
        else if (*q == '}') {
            depth--;
            if (depth == 0) { obj_len = (size_t)(q - obj) + 1u; break; }
        }
    }
    if (obj_len == 0u) return P4_OTA_PULL_MANIFEST_MALFORMED;   /* unbalanced */
    if (key_occurrences(obj, obj_len, "url") != 1u ||
        key_occurrences(obj, obj_len, "size") != 1u ||
        key_occurrences(obj, obj_len, "sha256") != 1u) {
        return P4_OTA_PULL_MANIFEST_MALFORMED;
    }

    if (!seek_key(obj, obj_len, "url", &c)) return P4_OTA_PULL_MANIFEST_MALFORMED;
    rc = copy_string(c, out->url, P4_OTA_PULL_URL_MAX);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;
    if (!relative_bundle_url_valid(out->url)) {
        return P4_OTA_PULL_MANIFEST_BAD_VALUE;
    }

    if (!seek_key(obj, obj_len, "size", &c)) return P4_OTA_PULL_MANIFEST_MALFORMED;
    rc = read_u32(c, &out->size);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;

    if (!seek_key(obj, obj_len, "sha256", &c)) return P4_OTA_PULL_MANIFEST_MALFORMED;
    rc = read_sha256(c, out->sha256);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;

    return P4_OTA_PULL_MANIFEST_OK;
}

const char *p4_ota_pull_manifest_result_name(p4_ota_pull_manifest_result_t r)
{
    switch (r) {
    case P4_OTA_PULL_MANIFEST_OK:             return "ok";
    case P4_OTA_PULL_MANIFEST_INVALID_ARG:    return "invalid-arg";
    case P4_OTA_PULL_MANIFEST_MALFORMED:      return "malformed";
    case P4_OTA_PULL_MANIFEST_UNSUPPORTED:    return "unsupported-schema";
    case P4_OTA_PULL_MANIFEST_NO_TARGET:      return "no-p4-target";
    case P4_OTA_PULL_MANIFEST_FIELD_TOO_LONG: return "field-too-long";
    case P4_OTA_PULL_MANIFEST_BAD_VALUE:      return "bad-value";
    }
    return "unknown";
}

typedef struct {
    uint32_t family;
    uint32_t major;
    uint32_t minor;
    uint32_t distance;
} release_version_t;

enum {
    RELEASE_FAMILY_RC = 1u,
    RELEASE_FAMILY_M = 2u,
};

static bool parse_u32_part(const char **cursor, uint32_t *out)
{
    if (!cursor || !*cursor || !out || !isdigit((unsigned char)**cursor)) {
        return false;
    }
    uint64_t value = 0u;
    const char *p = *cursor;
    while (isdigit((unsigned char)*p)) {
        value = value * 10u + (uint32_t)(*p - '0');
        if (value > UINT32_MAX) return false;
        p++;
    }
    *cursor = p;
    *out = (uint32_t)value;
    return true;
}

static bool parse_release_version(const char *text, release_version_t *out)
{
    if (!text || !out) return false;
    const char *p = text;
    release_version_t parsed = {0};
    if (p[0] == 'R' && p[1] == 'C') {
        parsed.family = RELEASE_FAMILY_RC;
        p += 2;
    } else if (p[0] == 'M') {
        parsed.family = RELEASE_FAMILY_M;
        p += 1;
    } else {
        return false;
    }
    if (!parse_u32_part(&p, &parsed.major)) return false;
    if (parsed.family == RELEASE_FAMILY_M && *p == '.') {
        p++;
        if (!parse_u32_part(&p, &parsed.minor)) return false;
    }
    /* A prerelease tag may itself be a git-describe string. Once that tag is
     * published, later builds become e.g.
     * M2.2-13-ge0f9add-11-ga1cc05c. Treat every distance/hash suffix as one
     * additional ancestry segment and sum the distances, preserving ordering
     * from the milestone instead of making all later builds incomparable. */
    while (*p != '\0' && strcmp(p, "-dirty") != 0) {
        uint32_t segment_distance = 0u;
        if (*p++ != '-' || !parse_u32_part(&p, &segment_distance) ||
            *p++ != '-' || *p++ != 'g') {
            return false;
        }
        if (segment_distance > UINT32_MAX - parsed.distance) return false;
        parsed.distance += segment_distance;

        size_t hash_digits = 0u;
        while (isxdigit((unsigned char)*p)) {
            hash_digits++;
            p++;
        }
        if (hash_digits < 7u) return false;
    }
    if (strcmp(p, "-dirty") != 0 && *p != '\0') return false;
    *out = parsed;
    return true;
}

p4_ota_pull_release_order_t p4_ota_pull_release_compare(
    const char *offered_version, const char *running_version)
{
    if (!offered_version || !running_version) {
        return P4_OTA_PULL_RELEASE_UNORDERED;
    }
    if (strcmp(offered_version, running_version) == 0) {
        return P4_OTA_PULL_RELEASE_SAME;
    }

    release_version_t offered;
    release_version_t running;
    if (!parse_release_version(offered_version, &offered) ||
        !parse_release_version(running_version, &running)) {
        return P4_OTA_PULL_RELEASE_UNORDERED;
    }
    if (offered.family != running.family) {
        return offered.family > running.family ? P4_OTA_PULL_RELEASE_NEWER
                                                : P4_OTA_PULL_RELEASE_OLDER;
    }
    if (offered.major != running.major) {
        return offered.major > running.major ? P4_OTA_PULL_RELEASE_NEWER
                                             : P4_OTA_PULL_RELEASE_OLDER;
    }
    if (offered.minor != running.minor) {
        return offered.minor > running.minor ? P4_OTA_PULL_RELEASE_NEWER
                                             : P4_OTA_PULL_RELEASE_OLDER;
    }
    if (offered.distance != running.distance) {
        return offered.distance > running.distance ? P4_OTA_PULL_RELEASE_NEWER
                                                    : P4_OTA_PULL_RELEASE_OLDER;
    }
    /* Same position with a different hash means the histories disagree. */
    return P4_OTA_PULL_RELEASE_UNORDERED;
}

p4_ota_pull_release_order_t p4_ota_pull_manifest_order(
    const p4_ota_pull_manifest_t *m, const char *running_version)
{
    if (!m) return P4_OTA_PULL_RELEASE_UNORDERED;
    return p4_ota_pull_release_compare(m->release, running_version);
}

p4_ota_pull_bundle_release_result_t p4_ota_pull_validate_bundle_release(
    const char *offered_version,
    const char *signed_version,
    const char *running_version)
{
    if (!offered_version || !signed_version || !running_version ||
        offered_version[0] == '\0' || signed_version[0] == '\0' ||
        running_version[0] == '\0') {
        return P4_OTA_PULL_BUNDLE_RELEASE_INVALID_ARG;
    }
    if (strcmp(offered_version, signed_version) != 0) {
        return P4_OTA_PULL_BUNDLE_RELEASE_MISMATCH;
    }
    if (p4_ota_pull_release_compare(signed_version, running_version) !=
        P4_OTA_PULL_RELEASE_NEWER) {
        return P4_OTA_PULL_BUNDLE_RELEASE_NOT_NEWER;
    }
    return P4_OTA_PULL_BUNDLE_RELEASE_OK;
}

bool p4_ota_pull_offer_fresh(uint32_t now_ticks,
                             uint32_t offered_at_ticks,
                             uint32_t ttl_ticks)
{
    return ttl_ticks > 0u &&
           (uint32_t)(now_ticks - offered_at_ticks) <= ttl_ticks;
}
