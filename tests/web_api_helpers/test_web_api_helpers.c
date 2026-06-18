#include <assert.h>
#include <stdio.h>
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

int main(void)
{
    test_json_escape_handles_quotes_backslash_and_controls();
    test_json_escape_truncates_and_terminates();
    test_json_escape_accepts_null_and_zero_buffer();

    puts("web_api_helpers tests passed");
    return 0;
}
