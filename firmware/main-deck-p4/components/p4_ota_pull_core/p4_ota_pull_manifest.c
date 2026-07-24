#include "p4_ota_pull_manifest.h"

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

static void skip_ws(cur_t *c)
{
    while (c->p < c->end &&
           (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n')) {
        c->p++;
    }
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

p4_ota_pull_manifest_result_t p4_ota_pull_manifest_parse(
    const char *json, size_t len, p4_ota_pull_manifest_t *out)
{
    if (!json || !out || len == 0u) return P4_OTA_PULL_MANIFEST_INVALID_ARG;
    memset(out, 0, sizeof(*out));

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

    if (!seek_key(obj, obj_len, "url", &c)) return P4_OTA_PULL_MANIFEST_MALFORMED;
    rc = copy_string(c, out->url, P4_OTA_PULL_URL_MAX);
    if (rc != P4_OTA_PULL_MANIFEST_OK) return rc;

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

bool p4_ota_pull_manifest_differs(const p4_ota_pull_manifest_t *m,
                                  const char *running_version)
{
    if (!m || !running_version) return false;
    return strncmp(m->release, running_version, P4_OTA_PULL_RELEASE_MAX) != 0;
}
