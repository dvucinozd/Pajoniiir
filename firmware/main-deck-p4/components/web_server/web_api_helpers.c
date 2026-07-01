#include "web_api_helpers.h"

#include <stdio.h>

static void append_json_char(char c, char *dst, size_t dst_size, size_t *written)
{
    if (dst && *written + 1u < dst_size) {
        dst[*written] = c;
    }
    (*written)++;
}

size_t web_api_json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t written = 0;

    if (!src) {
        src = "";
    }

    for (size_t i = 0; src[i] != '\0'; i++) {
        switch (src[i]) {
        case '"':
            append_json_char('\\', dst, dst_size, &written);
            append_json_char('"', dst, dst_size, &written);
            break;
        case '\\':
            append_json_char('\\', dst, dst_size, &written);
            append_json_char('\\', dst, dst_size, &written);
            break;
        case '\n':
            append_json_char('\\', dst, dst_size, &written);
            append_json_char('n', dst, dst_size, &written);
            break;
        case '\r':
            append_json_char('\\', dst, dst_size, &written);
            append_json_char('r', dst, dst_size, &written);
            break;
        case '\t':
            append_json_char('\\', dst, dst_size, &written);
            append_json_char('t', dst, dst_size, &written);
            break;
        default:
            append_json_char(src[i], dst, dst_size, &written);
            break;
        }
    }

    if (dst_size > 0 && dst) {
        size_t terminator = written < dst_size ? written : dst_size - 1u;
        dst[terminator] = '\0';
    }

    return written;
}

int web_api_format_beat_fx_json(char *dst,
                                size_t dst_size,
                                int effect,
                                int beat,
                                int target,
                                unsigned depth,
                                bool enabled)
{
    if (!dst || dst_size == 0) {
        return 0;
    }
    return snprintf(dst,
                    dst_size,
                    "\"beat_fx\":{\"effect\":%d,\"beat\":%d,\"target\":%d,\"depth\":%u,\"enabled\":%s}",
                    effect,
                    beat,
                    target,
                    depth,
                    enabled ? "true" : "false");
}
