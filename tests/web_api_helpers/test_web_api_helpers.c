#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_api_helpers.h"

static void test_json_escape_handles_quotes_backslash_and_controls(void)
{
    char out[128];

    size_t n = web_api_json_escape("A \"quote\" \\\\ line\n\tend", out, sizeof(out));

    assert(strcmp(out, "A \\\"quote\\\" \\\\\\\\ line\\n\\tend") == 0);
    assert(n == strlen(out));
}

static void test_json_escape_truncates_and_terminates(void)
{
    char out[8];

    size_t n = web_api_json_escape("123456789", out, sizeof(out));

    assert(strcmp(out, "1234567") == 0);
    assert(n == 9);
}

static void test_json_escape_accepts_null_and_zero_buffer(void)
{
    char out[4] = {'x', 'x', 'x', 'x'};

    assert(web_api_json_escape(NULL, out, sizeof(out)) == 0);
    assert(strcmp(out, "") == 0);
    assert(web_api_json_escape("abc", NULL, 0) == 3);
}

static void test_json_escape_encodes_all_control_characters(void)
{
    char source[] = { 'a', '\x01', '\b', 'z', '\0' };
    char out[32];

    size_t n = web_api_json_escape(source, out, sizeof(out));

    assert(strcmp(out, "a\\u0001\\u0008z") == 0);
    assert(n == strlen(out));
}

static void test_strict_int_parser_rejects_partial_and_out_of_range_values(void)
{
    int32_t value = -1;

    assert(web_api_parse_int32("0", 0, 16383, &value) && value == 0);
    assert(web_api_parse_int32("16383", 0, 16383, &value) && value == 16383);
    assert(web_api_parse_int32("-12", -20, 20, &value) && value == -12);
    assert(!web_api_parse_int32(NULL, 0, 10, &value));
    assert(!web_api_parse_int32("", 0, 10, &value));
    assert(!web_api_parse_int32(" 1", 0, 10, &value));
    assert(!web_api_parse_int32("+1", 0, 10, &value));
    assert(!web_api_parse_int32("1x", 0, 10, &value));
    assert(!web_api_parse_int32("11", 0, 10, &value));
    assert(!web_api_parse_int32("999999999999999999999", 0, INT32_MAX, &value));
    assert(!web_api_parse_int32("1", 10, 0, &value));
    assert(!web_api_parse_int32("1", 0, 10, NULL));
}

static void test_beat_fx_json_formats_status_block(void)
{
    char out[128];

    int n = web_api_format_beat_fx_json(out, sizeof(out), 2, 3, 1, 42, true);

    assert(n > 0);
    assert(strcmp(out, "\"beat_fx\":{\"effect\":2,\"beat\":3,\"target\":1,\"depth\":42,\"enabled\":true}") == 0);
}

static void test_beat_fx_echo_diag_json_formats_status_block(void)
{
    char out[160];

    int n = web_api_format_beat_fx_echo_diag_json(out,
                                                  sizeof(out),
                                                  true,
                                                  false,
                                                  true,
                                                  false,
                                                  250,
                                                  500);

    assert(n > 0);
    assert(strcmp(out, "\"beat_fx_echo\":{\"allocated1\":true,\"allocated2\":false,\"enabled1\":true,\"enabled2\":false,\"delay_ms1\":250,\"delay_ms2\":500}") == 0);
}

static void test_alloc_printf_handles_payload_larger_than_legacy_status_buffer(void)
{
    char large[2300];
    memset(large, 'x', sizeof(large) - 1u);
    large[sizeof(large) - 1u] = '\0';

    char *out = NULL;
    int n = web_api_alloc_printf(&out, "{\"status\":\"%s\"}", large);

    assert(out != NULL);
    assert(n == (int)strlen(out));
    assert((size_t)n > 2048u);
    assert(strncmp(out, "{\"status\":\"", 11u) == 0);
    free(out);
}

static void test_clamp_seek_ms_bounds_to_loaded_track_duration(void)
{
    assert(web_api_clamp_seek_ms(-10, 30000u, true) == 0u);
    assert(web_api_clamp_seek_ms(12000, 30000u, true) == 12000u);
    assert(web_api_clamp_seek_ms(40000, 30000u, true) == 30000u);
    assert(web_api_clamp_seek_ms(40000, 0u, false) == 40000u);
}

static void test_controller_json_formats_connected_and_absent(void)
{
    char out[256];

    web_api_format_controller_json(out, sizeof(out), true, 0x2B73, 0x0045,
                                   "Pioneer DDJ-FLX4", true, true, true,
                                   "pioneer_ddj_flx4", "active", 2);
    assert(strcmp(out,
        "\"controller\":{\"present\":true,\"vid\":\"0x2B73\",\"pid\":\"0x0045\","
        "\"product\":\"Pioneer DDJ-FLX4\",\"midi_in\":true,\"midi_out\":true,"
        "\"usb_audio\":true,\"active_profile\":\"pioneer_ddj_flx4\","
        "\"profile_state\":\"active\",\"profiles\":2}") == 0);

    /* No controller / unsupported (no active profile). */
    web_api_format_controller_json(out, sizeof(out), false, 0, 0, "",
                                   false, false, false, "", "idle", 0);
    assert(strcmp(out,
        "\"controller\":{\"present\":false,\"vid\":\"0x0000\",\"pid\":\"0x0000\","
        "\"product\":\"\",\"midi_in\":false,\"midi_out\":false,"
        "\"usb_audio\":false,\"active_profile\":\"\","
        "\"profile_state\":\"idle\",\"profiles\":0}") == 0);
}

static void test_profile_upload_policy_is_bounded_and_explicit(void)
{
    bool overwrite = true;

    assert(!web_api_profile_content_length_valid(0));
    assert(!web_api_profile_content_length_valid(WEB_API_PROFILE_MIN_SIZE - 1u));
    assert(web_api_profile_content_length_valid(WEB_API_PROFILE_MIN_SIZE));
    assert(web_api_profile_content_length_valid(WEB_API_PROFILE_MAX_SIZE));
    assert(!web_api_profile_content_length_valid(WEB_API_PROFILE_MAX_SIZE + 1u));

    assert(web_api_profile_overwrite_parse(NULL, &overwrite) && !overwrite);
    assert(web_api_profile_overwrite_parse("0", &overwrite) && !overwrite);
    assert(web_api_profile_overwrite_parse("1", &overwrite) && overwrite);
    assert(!web_api_profile_overwrite_parse("true", &overwrite));
    assert(!web_api_profile_overwrite_parse("01", &overwrite));
    assert(!web_api_profile_overwrite_parse("1 ", &overwrite));
    assert(!web_api_profile_overwrite_parse("1", NULL));
}

int main(void)
{
    test_json_escape_handles_quotes_backslash_and_controls();
    test_json_escape_truncates_and_terminates();
    test_json_escape_accepts_null_and_zero_buffer();
    test_json_escape_encodes_all_control_characters();
    test_strict_int_parser_rejects_partial_and_out_of_range_values();
    test_beat_fx_json_formats_status_block();
    test_beat_fx_echo_diag_json_formats_status_block();
    test_alloc_printf_handles_payload_larger_than_legacy_status_buffer();
    test_clamp_seek_ms_bounds_to_loaded_track_duration();
    test_controller_json_formats_connected_and_absent();
    test_profile_upload_policy_is_bounded_and_explicit();

    puts("web_api_helpers tests passed");
    return 0;
}
