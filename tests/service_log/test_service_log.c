/*
 * Host tests for the pure P4 service-log formatting/schema core.
 *
 * Covers event-name/severity lookup, control-character sanitization, key=value
 * record formatting (arg-count handling, message inclusion, trailing newline),
 * the boot header line and the rotation-size decision. No file or queue work.
 */
#include "service_log_format.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                 \
            s_failures++;                                                      \
        }                                                                      \
    } while (0)

static void test_names_and_severity(void)
{
    printf("== event names + severity ==\n");
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_BOOT), "BOOT") == 0);
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_TRACK_LOAD_DONE), "TRACK_LOAD_DONE") == 0);
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_WEB_LOAD_REQ_FAILED), "WEB_LOAD_REQUEST_FAILED") == 0);
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_CONTROLLER_DISCONNECTED), "CONTROLLER_DISCONNECTED") == 0);
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_CONTROL_LINK_CRC), "CONTROL_LINK_CRC_ERROR") == 0);
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_CONTROL_LINK_GAP), "CONTROL_LINK_GAP") == 0);
    CHECK(strcmp(service_log_event_name(SERVICE_LOG_EVENT_COUNT), "UNKNOWN") == 0);
    /* every id resolves to a non-empty, non-UNKNOWN name */
    for (int e = 0; e < SERVICE_LOG_EVENT_COUNT; e++) {
        const char *n = service_log_event_name((service_log_event_t)e);
        CHECK(n[0] != '\0' && strcmp(n, "UNKNOWN") != 0);
    }
    CHECK(service_log_severity_char(SERVICE_LOG_INFO) == 'I');
    CHECK(service_log_severity_char(SERVICE_LOG_WARN) == 'W');
    CHECK(service_log_severity_char(SERVICE_LOG_ERROR) == 'E');
}

static void test_sanitize(void)
{
    printf("== sanitize ==\n");
    char buf[16];
    CHECK(service_log_sanitize(buf, sizeof(buf), "ok text") == 7);
    CHECK(strcmp(buf, "ok text") == 0);

    /* newline / CR / tab / control bytes become '.' */
    service_log_sanitize(buf, sizeof(buf), "a\nb\r\tc");
    CHECK(strcmp(buf, "a.b..c") == 0);

    /* truncation keeps within dst_len and stays NUL-terminated */
    size_t n = service_log_sanitize(buf, 4, "abcdef");
    CHECK(n == 3 && strcmp(buf, "abc") == 0);

    /* NULL src -> empty */
    CHECK(service_log_sanitize(buf, sizeof(buf), NULL) == 0 && buf[0] == '\0');
}

static void test_format_record(void)
{
    printf("== format_record ==\n");
    char line[SERVICE_LOG_LINE_MAX];

    /* two args + message */
    service_log_record_t r = {0};
    r.seq = 381; r.boot_id = 42; r.uptime_ms = 184230;
    r.event = SERVICE_LOG_TRACK_LOAD_DONE; r.severity = SERVICE_LOG_INFO;
    r.arg_count = 2; r.args[0] = 1; r.args[1] = 832;
    snprintf(r.text, sizeof(r.text), "cache=hit");

    int len = service_log_format_record(line, sizeof(line), &r);
    CHECK(len > 0 && line[len - 1] == '\n');
    CHECK(strstr(line, "seq=381 boot=42 ms=184230 level=I event=TRACK_LOAD_DONE") == line);
    CHECK(strstr(line, " a0=1 a1=832") != NULL);
    CHECK(strstr(line, " msg=cache=hit") != NULL);
    CHECK(strstr(line, "a2=") == NULL);   /* arg_count respected */

    /* zero args, no text -> no a0, no msg */
    service_log_record_t z = {0};
    z.seq = 1; z.event = SERVICE_LOG_SD_MOUNTED; z.severity = SERVICE_LOG_WARN;
    len = service_log_format_record(line, sizeof(line), &z);
    CHECK(len > 0);
    CHECK(strstr(line, "level=W event=SD_MOUNTED") != NULL);
    CHECK(strstr(line, " a0=") == NULL);
    CHECK(strstr(line, " msg=") == NULL);

    /* message with control chars gets sanitized in the line */
    service_log_record_t m = {0};
    m.event = SERVICE_LOG_SD_ERROR; m.severity = SERVICE_LOG_ERROR;
    snprintf(m.text, sizeof(m.text), "bad\nline");
    len = service_log_format_record(line, sizeof(line), &m);
    CHECK(len > 0 && line[len - 1] == '\n');
    CHECK(strstr(line, "msg=bad.line") != NULL);
    /* only the single trailing newline, none embedded */
    CHECK(strchr(line, '\n') == &line[len - 1]);
}

static void test_format_header(void)
{
    printf("== format_header ==\n");
    char hdr[SERVICE_LOG_LINE_MAX];
    int len = service_log_format_header(hdr, sizeof(hdr), 42,
                                        "RC1-147-gb9bc8134", "ota_1", "PANIC");
    CHECK(len > 0 && hdr[len - 1] == '\n');
    CHECK(strstr(hdr, "schema=1 boot=42 event=FIRMWARE_INFO") == hdr);
    CHECK(strstr(hdr, "fw=RC1-147-gb9bc8134") != NULL);
    CHECK(strstr(hdr, "partition=ota_1") != NULL);
    CHECK(strstr(hdr, "reset=PANIC") != NULL);

    /* NULL fields fall back to "?" */
    len = service_log_format_header(hdr, sizeof(hdr), 1, NULL, NULL, NULL);
    CHECK(len > 0 && strstr(hdr, "fw=? partition=? reset=?") != NULL);
}

static void test_should_rotate(void)
{
    printf("== should_rotate ==\n");
    CHECK(!service_log_should_rotate(0, SERVICE_LOG_MAX_FILE_BYTES));
    CHECK(!service_log_should_rotate(SERVICE_LOG_MAX_FILE_BYTES - 1, SERVICE_LOG_MAX_FILE_BYTES));
    CHECK(service_log_should_rotate(SERVICE_LOG_MAX_FILE_BYTES, SERVICE_LOG_MAX_FILE_BYTES));
    CHECK(service_log_should_rotate(SERVICE_LOG_MAX_FILE_BYTES + 1, SERVICE_LOG_MAX_FILE_BYTES));
    CHECK(!service_log_should_rotate(1000, 0));   /* disabled */
}

int main(void)
{
    printf("=== service_log tests ===\n");
    test_names_and_severity();
    test_sanitize();
    test_format_record();
    test_format_header();
    test_should_rotate();

    if (s_failures == 0) {
        printf("service_log tests passed\n");
        return 0;
    }
    printf("service_log tests FAILED (%d)\n", s_failures);
    return 1;
}
