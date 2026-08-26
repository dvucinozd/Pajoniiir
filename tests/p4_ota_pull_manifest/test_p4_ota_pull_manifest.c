#include "p4_ota_pull_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SHA "83ba9811ff2e455de21f29b237ca9d96c6ff70aa0f53c5ae01213c38802cbe22"

static const char *GOOD =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"release\": \"RC1-237-g7bf0fd3c\",\n"
    "  \"p4\": {\n"
    "    \"url\": \"RC1-237-g7bf0fd3c/main-deck-p4.ddjota\",\n"
    "    \"size\": 2147132,\n"
    "    \"sha256\": \"" SHA "\"\n"
    "  }\n"
    "}\n";

static p4_ota_pull_manifest_result_t parse(const char *s, p4_ota_pull_manifest_t *m)
{
    return p4_ota_pull_manifest_parse(s, strlen(s), m);
}

static void test_parses_the_document_the_publisher_writes(void)
{
    p4_ota_pull_manifest_t m;
    assert(parse(GOOD, &m) == P4_OTA_PULL_MANIFEST_OK);
    assert(strcmp(m.release, "RC1-237-g7bf0fd3c") == 0);
    assert(strcmp(m.url, "RC1-237-g7bf0fd3c/main-deck-p4.ddjota") == 0);
    assert(m.size == 2147132u);
    assert(m.sha256[0] == 0x83 && m.sha256[1] == 0xba);
    assert(m.sha256[30] == 0xbe && m.sha256[31] == 0x22);
}

static void test_release_comparison_is_newer_only_and_monotonic(void)
{
    p4_ota_pull_manifest_t m;
    assert(parse(GOOD, &m) == P4_OTA_PULL_MANIFEST_OK);
    assert(p4_ota_pull_manifest_order(&m, "RC1-237-g7bf0fd3c") ==
           P4_OTA_PULL_RELEASE_SAME);
    assert(p4_ota_pull_manifest_order(&m, "RC1-236-gac97f145") ==
           P4_OTA_PULL_RELEASE_NEWER);
    assert(p4_ota_pull_manifest_order(&m, "RC1-999-gffffffff") ==
           P4_OTA_PULL_RELEASE_OLDER);

    assert(p4_ota_pull_release_compare("RC2", "RC1-999-gffffffff") ==
           P4_OTA_PULL_RELEASE_NEWER);
    assert(p4_ota_pull_release_compare("RC1-1-gabcdef0", "RC2") ==
           P4_OTA_PULL_RELEASE_OLDER);
    assert(p4_ota_pull_release_compare("RC1-7-gabcdef0",
                                       "RC1-7-g1234567") ==
           P4_OTA_PULL_RELEASE_UNORDERED);
    assert(p4_ota_pull_release_compare("custom", "RC1") ==
           P4_OTA_PULL_RELEASE_UNORDERED);
    assert(p4_ota_pull_release_compare(NULL, "RC1") ==
           P4_OTA_PULL_RELEASE_UNORDERED);
    assert(p4_ota_pull_manifest_order(NULL, "RC1") ==
           P4_OTA_PULL_RELEASE_UNORDERED);
}

static void test_offer_freshness_is_wrap_safe(void)
{
    assert(p4_ota_pull_offer_fresh(150u, 100u, 50u));
    assert(!p4_ota_pull_offer_fresh(151u, 100u, 50u));
    assert(!p4_ota_pull_offer_fresh(100u, 100u, 0u));
    assert(p4_ota_pull_offer_fresh(5u, UINT32_MAX - 4u, 10u));
}

static void test_signed_bundle_release_is_bound_to_offer_and_newer_only(void)
{
    assert(p4_ota_pull_validate_bundle_release(
               "RC2-12-gabcdef0", "RC2-12-gabcdef0", "RC2-11-g1234567") ==
           P4_OTA_PULL_BUNDLE_RELEASE_OK);

    /* A newer channel cannot smuggle a validly signed old bundle. */
    assert(p4_ota_pull_validate_bundle_release(
               "RC2-12-gabcdef0", "RC2-10-g7654321", "RC2-11-g1234567") ==
           P4_OTA_PULL_BUNDLE_RELEASE_MISMATCH);

    /* Prefix equality is not equality. */
    assert(p4_ota_pull_validate_bundle_release(
               "RC2-12-gabcdef0-extra", "RC2-12-gabcdef0", "RC2-11-g1234567") ==
           P4_OTA_PULL_BUNDLE_RELEASE_MISMATCH);

    assert(p4_ota_pull_validate_bundle_release(
               "RC2-11-g1234567", "RC2-11-g1234567", "RC2-11-g1234567") ==
           P4_OTA_PULL_BUNDLE_RELEASE_NOT_NEWER);
    assert(p4_ota_pull_validate_bundle_release(
               "RC2-10-g7654321", "RC2-10-g7654321", "RC2-11-g1234567") ==
           P4_OTA_PULL_BUNDLE_RELEASE_NOT_NEWER);
    assert(p4_ota_pull_validate_bundle_release(NULL, "RC2", "RC1") ==
           P4_OTA_PULL_BUNDLE_RELEASE_INVALID_ARG);
}

static void test_unknown_schema_is_refused_not_guessed(void)
{
    p4_ota_pull_manifest_t m;
    const char *j = "{\"schema_version\":2,\"release\":\"a\",\"p4\":{"
                    "\"url\":\"u\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(j, &m) == P4_OTA_PULL_MANIFEST_UNSUPPORTED);
}

static void test_a_document_without_a_p4_target_is_distinguishable(void)
{
    p4_ota_pull_manifest_t m;
    const char *j = "{\"schema_version\":1,\"release\":\"a\",\"s3\":{"
                    "\"url\":\"u\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    /* Not malformed - it is a valid channel document that simply has nothing
     * for this board, and the UI should say so rather than report an error. */
    assert(parse(j, &m) == P4_OTA_PULL_MANIFEST_NO_TARGET);
}

/* A url belonging to a different target must not be picked up: the extractor
 * is depth-blind, so the p4 object's extent is what bounds the lookups. */
static void test_fields_are_read_from_the_p4_object_only(void)
{
    p4_ota_pull_manifest_t m;
    const char *j =
        "{\"schema_version\":1,\"release\":\"r\","
        "\"p4\":{\"url\":\"correct.ddjota\",\"size\":10,\"sha256\":\"" SHA "\"},"
        "\"s3\":{\"url\":\"wrong.ddjota\",\"size\":99,\"sha256\":\"" SHA "\"}}";
    assert(parse(j, &m) == P4_OTA_PULL_MANIFEST_OK);
    assert(strcmp(m.url, "correct.ddjota") == 0);
    assert(m.size == 10u);

    /* And with the targets the other way round, so the test cannot pass by
     * accident of ordering. */
    const char *k =
        "{\"schema_version\":1,\"release\":\"r\","
        "\"s3\":{\"url\":\"wrong.ddjota\",\"size\":99,\"sha256\":\"" SHA "\"},"
        "\"p4\":{\"url\":\"correct.ddjota\",\"size\":10,\"sha256\":\"" SHA "\"}}";
    assert(parse(k, &m) == P4_OTA_PULL_MANIFEST_OK);
    assert(strcmp(m.url, "correct.ddjota") == 0);
    assert(m.size == 10u);
}

static void test_hostile_and_truncated_input_is_rejected_not_read_past(void)
{
    p4_ota_pull_manifest_t m;
    /* Unterminated string: must not run off the end of the buffer. */
    const char *unterminated = "{\"schema_version\":1,\"release\":\"abc";
    assert(parse(unterminated, &m) == P4_OTA_PULL_MANIFEST_MALFORMED);

    /* Unbalanced target object. */
    const char *unbalanced =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{\"url\":\"u\"";
    assert(parse(unbalanced, &m) == P4_OTA_PULL_MANIFEST_MALFORMED);

    /* Every prefix of a good document must fail cleanly rather than crash or
     * half-populate. */
    size_t n = strlen(GOOD);
    for (size_t cut = 1; cut < n; cut++) {
        p4_ota_pull_manifest_t partial;
        p4_ota_pull_manifest_result_t rc =
            p4_ota_pull_manifest_parse(GOOD, cut, &partial);
        if (rc == P4_OTA_PULL_MANIFEST_OK) {
            /* Only a complete document may parse; the last useful byte is the
             * closing quote of the hash, so anything shorter must not succeed. */
            assert(cut >= n - 4u);
        }
    }
}

static void test_bad_values_are_refused(void)
{
    p4_ota_pull_manifest_t m;
    const char *short_hash =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"u\",\"size\":1,\"sha256\":\"abcd\"}}";
    assert(parse(short_hash, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *bad_hex =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"u\",\"size\":1,\"sha256\":\"zz"
        "ba9811ff2e455de21f29b237ca9d96c6ff70aa0f53c5ae01213c38802cbe22\"}}";
    assert(parse(bad_hex, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    /* Zero size would let a "successful" empty download look valid. */
    const char *zero_size =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"u\",\"size\":0,\"sha256\":\"" SHA "\"}}";
    assert(parse(zero_size, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *empty_url =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(empty_url, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    /* Escapes are refused rather than unescaped: no field here needs one, and
     * implementing unescaping on network input buys nothing. */
    const char *escaped =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"a\\/b\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(escaped, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *absolute_url =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"https://evil.example/fw.ddjota\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(absolute_url, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *traversal =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"../old/fw.ddjota\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(traversal, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *encoded_traversal =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"%2e%2e/old/fw.ddjota\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(encoded_traversal, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *scheme_like_path =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"https:evil.example/fw.ddjota\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(scheme_like_path, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);

    const char *query =
        "{\"schema_version\":1,\"release\":\"r\",\"p4\":{"
        "\"url\":\"fw.ddjota?redirect=x\",\"size\":1,\"sha256\":\"" SHA "\"}}";
    assert(parse(query, &m) == P4_OTA_PULL_MANIFEST_BAD_VALUE);
}

static void test_oversized_fields_are_refused_not_truncated(void)
{
    static char big[1024];
    int n = snprintf(big, sizeof(big),
                     "{\"schema_version\":1,\"release\":\"r\",\"p4\":{\"url\":\"");
    for (size_t i = 0; i < P4_OTA_PULL_URL_MAX + 8u; i++) big[n++] = 'x';
    n += snprintf(big + n, sizeof(big) - (size_t)n,
                  "\",\"size\":1,\"sha256\":\"" SHA "\"}}");
    (void)n;
    p4_ota_pull_manifest_t m;
    assert(parse(big, &m) == P4_OTA_PULL_MANIFEST_FIELD_TOO_LONG);
}

static void test_null_and_empty_are_inert(void)
{
    p4_ota_pull_manifest_t m;
    assert(p4_ota_pull_manifest_parse(NULL, 10, &m) == P4_OTA_PULL_MANIFEST_INVALID_ARG);
    assert(p4_ota_pull_manifest_parse("{}", 2, NULL) == P4_OTA_PULL_MANIFEST_INVALID_ARG);
    assert(p4_ota_pull_manifest_parse("{}", 0, &m) == P4_OTA_PULL_MANIFEST_INVALID_ARG);
    assert(p4_ota_pull_manifest_parse("{}", 2, &m) == P4_OTA_PULL_MANIFEST_MALFORMED);
    assert(strcmp(p4_ota_pull_manifest_result_name(P4_OTA_PULL_MANIFEST_OK), "ok") == 0);
}

int main(void)
{
    test_parses_the_document_the_publisher_writes();
    test_release_comparison_is_newer_only_and_monotonic();
    test_offer_freshness_is_wrap_safe();
    test_signed_bundle_release_is_bound_to_offer_and_newer_only();
    test_unknown_schema_is_refused_not_guessed();
    test_a_document_without_a_p4_target_is_distinguishable();
    test_fields_are_read_from_the_p4_object_only();
    test_hostile_and_truncated_input_is_rejected_not_read_past();
    test_bad_values_are_refused();
    test_oversized_fields_are_refused_not_truncated();
    test_null_and_empty_are_inert();
    puts("p4_ota_pull_manifest tests passed");
    return 0;
}
