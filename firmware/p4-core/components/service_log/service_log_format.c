#include "service_log_format.h"

#include <stdio.h>
#include <string.h>

static const char *const s_event_names[] = {
#define X(id, name) name,
    SERVICE_LOG_EVENTS(X)
#undef X
};

const char *service_log_event_name(service_log_event_t event)
{
    if ((unsigned)event >= (unsigned)SERVICE_LOG_EVENT_COUNT) {
        return "UNKNOWN";
    }
    return s_event_names[event];
}

char service_log_severity_char(service_log_severity_t severity)
{
    switch (severity) {
    case SERVICE_LOG_WARN:  return 'W';
    case SERVICE_LOG_ERROR: return 'E';
    case SERVICE_LOG_INFO:
    default:                return 'I';
    }
}

size_t service_log_sanitize(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0u) {
        return 0u;
    }
    size_t i = 0u;
    if (src) {
        while (src[i] != '\0' && i + 1u < dst_len) {
            unsigned char c = (unsigned char)src[i];
            /* Replace CR/LF and any non-printable control byte with '.'. */
            dst[i] = (c < 0x20u || c == 0x7Fu) ? '.' : (char)c;
            i++;
        }
    }
    dst[i] = '\0';
    return i;
}

int service_log_format_record(char *out, size_t out_len,
                              const service_log_record_t *rec)
{
    if (!out || out_len == 0u || !rec) {
        return 0;
    }

    int n = snprintf(out, out_len,
                     "seq=%lu boot=%lu ms=%lu level=%c event=%s",
                     (unsigned long)rec->seq, (unsigned long)rec->boot_id,
                     (unsigned long)rec->uptime_ms,
                     service_log_severity_char(rec->severity),
                     service_log_event_name(rec->event));
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    size_t len = (size_t)n;

    uint8_t argc = rec->arg_count > SERVICE_LOG_ARG_MAX ? SERVICE_LOG_ARG_MAX
                                                        : rec->arg_count;
    for (uint8_t a = 0u; a < argc; a++) {
        int m = snprintf(out + len, out_len - len, " a%u=%lu",
                         (unsigned)a, (unsigned long)rec->args[a]);
        if (m < 0 || (size_t)m >= out_len - len) {
            out[0] = '\0';
            return 0;
        }
        len += (size_t)m;
    }

    if (rec->text[0] != '\0') {
        char safe[SERVICE_LOG_TEXT_MAX];
        service_log_sanitize(safe, sizeof(safe), rec->text);
        int m = snprintf(out + len, out_len - len, " msg=%s", safe);
        if (m < 0 || (size_t)m >= out_len - len) {
            out[0] = '\0';
            return 0;
        }
        len += (size_t)m;
    }

    if (len + 1u >= out_len) {   /* room for the trailing newline */
        out[0] = '\0';
        return 0;
    }
    out[len++] = '\n';
    out[len] = '\0';
    return (int)len;
}

int service_log_format_header(char *out, size_t out_len, uint32_t boot_id,
                              const char *fw_version, const char *partition,
                              const char *reset_reason)
{
    if (!out || out_len == 0u) {
        return 0;
    }
    char v[SERVICE_LOG_TEXT_MAX], p[SERVICE_LOG_TEXT_MAX], r[SERVICE_LOG_TEXT_MAX];
    service_log_sanitize(v, sizeof(v), fw_version ? fw_version : "?");
    service_log_sanitize(p, sizeof(p), partition ? partition : "?");
    service_log_sanitize(r, sizeof(r), reset_reason ? reset_reason : "?");

    int n = snprintf(out, out_len,
                     "schema=%u boot=%lu event=%s fw=%s partition=%s reset=%s\n",
                     SERVICE_LOG_SCHEMA_VERSION, (unsigned long)boot_id,
                     service_log_event_name(SERVICE_LOG_FIRMWARE_INFO), v, p, r);
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return n;
}

bool service_log_should_rotate(uint64_t current_bytes, uint32_t max_bytes)
{
    return max_bytes != 0u && current_bytes >= (uint64_t)max_bytes;
}
