#include "s3_debug_ap.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_empty_snapshot(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    char out[64];
    size_t n = s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(n == 0);
    assert(out[0] == '\0');
}

static void test_append_and_snapshot(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    s3_debug_log_ring_append(&ring, "one\n");
    s3_debug_log_ring_append(&ring, "two\n");

    char out[64];
    size_t n = s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(n == strlen("one\ntwo\n"));
    assert(strcmp(out, "one\ntwo\n") == 0);
}

static void test_truncates_long_line(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    char long_line[400];
    memset(long_line, 'A', sizeof(long_line));
    long_line[sizeof(long_line) - 1] = '\0';

    s3_debug_log_ring_append(&ring, long_line);

    char out[512];
    size_t n = s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(n == S3_DEBUG_LOG_LINE_MAX - 1);
    assert(out[n] == '\0');
}

static void test_overflow_keeps_newest_lines(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    for (int i = 0; i < S3_DEBUG_LOG_RING_LINES + 4; i++) {
        char line[32];
        snprintf(line, sizeof(line), "line-%02d\n", i);
        s3_debug_log_ring_append(&ring, line);
    }

    char out[2048];
    (void)s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(strstr(out, "line-00\n") == NULL);
    assert(strstr(out, "line-03\n") == NULL);
    assert(strstr(out, "line-04\n") != NULL);
    assert(strstr(out, "line-19\n") != NULL);
}

int main(void)
{
    test_empty_snapshot();
    test_append_and_snapshot();
    test_truncates_long_line();
    test_overflow_keeps_newest_lines();
    puts("s3_debug_log_ring tests passed");
    return 0;
}
