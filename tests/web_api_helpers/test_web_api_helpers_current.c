#define main web_api_helpers_legacy_main
#include "test_web_api_helpers.c"
#undef main

#include "web_firmware_json.h"

static void test_firmware_json_snapshot_escapes_in_place(void)
{
    char status[32] = "build \"test\" \\ line\n";
    web_firmware_json_escape_in_place(status, sizeof(status));
    assert(strcmp(status, "build \\\"test\\\" \\\\ line\\n") == 0);
}

static void test_firmware_json_snapshot_never_leaves_partial_escape(void)
{
    char status[5] = "\"x";
    web_firmware_json_escape_in_place(status, sizeof(status));
    assert(strcmp(status, "\\\"x") == 0);

    char tiny[3] = "\"";
    web_firmware_json_escape_in_place(tiny, sizeof(tiny));
    assert(strcmp(tiny, "\\\"") == 0);
}

int main(void)
{
    int rc = web_api_helpers_legacy_main();
    if (rc != 0) return rc;

    test_firmware_json_snapshot_escapes_in_place();
    test_firmware_json_snapshot_never_leaves_partial_escape();
    puts("web firmware JSON tests passed");
    return 0;
}
