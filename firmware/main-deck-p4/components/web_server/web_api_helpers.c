#include "web_api_helpers.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Appends a full escape sequence or nothing: a truncated two-byte escape
 * would leave a lone '\' that escapes the closing quote of the JSON string.
 * `written` counts the untruncated logical length (like snprintf's return),
 * `stored` counts bytes actually placed in dst; once they diverge the output
 * is truncated and no further bytes are stored. */
static void append_json_seq(const char *seq, size_t len,
                            char *dst, size_t dst_size,
                            size_t *written, size_t *stored)
{
    if (dst && *stored == *written && *stored + len < dst_size) {
        memcpy(&dst[*stored], seq, len);
        *stored += len;
    }
    *written += len;
}

size_t web_api_json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t written = 0;
    size_t stored = 0;

    if (!src) {
        src = "";
    }

    for (size_t i = 0; src[i] != '\0'; i++) {
        switch (src[i]) {
        case '"':
            append_json_seq("\\\"", 2u, dst, dst_size, &written, &stored);
            break;
        case '\\':
            append_json_seq("\\\\", 2u, dst, dst_size, &written, &stored);
            break;
        case '\n':
            append_json_seq("\\n", 2u, dst, dst_size, &written, &stored);
            break;
        case '\r':
            append_json_seq("\\r", 2u, dst, dst_size, &written, &stored);
            break;
        case '\t':
            append_json_seq("\\t", 2u, dst, dst_size, &written, &stored);
            break;
        default:
            append_json_seq(&src[i], 1u, dst, dst_size, &written, &stored);
            break;
        }
    }

    if (dst_size > 0 && dst) {
        dst[stored < dst_size ? stored : dst_size - 1u] = '\0';
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

int web_api_format_beat_fx_echo_diag_json(char *dst,
                                          size_t dst_size,
                                          bool allocated1,
                                          bool allocated2,
                                          bool enabled1,
                                          bool enabled2,
                                          unsigned delay_ms1,
                                          unsigned delay_ms2)
{
    if (!dst || dst_size == 0) {
        return 0;
    }
    return snprintf(dst,
                    dst_size,
                    "\"beat_fx_echo\":{\"allocated1\":%s,\"allocated2\":%s,\"enabled1\":%s,\"enabled2\":%s,\"delay_ms1\":%u,\"delay_ms2\":%u}",
                    allocated1 ? "true" : "false",
                    allocated2 ? "true" : "false",
                    enabled1 ? "true" : "false",
                    enabled2 ? "true" : "false",
                    delay_ms1,
                    delay_ms2);
}

int web_api_format_controller_json(char *dst,
                                   size_t dst_size,
                                   bool present,
                                   unsigned vid,
                                   unsigned pid,
                                   const char *product_escaped,
                                   bool midi_in,
                                   bool midi_out,
                                   bool usb_audio,
                                   const char *active_profile_escaped,
                                   unsigned profile_count)
{
    if (!dst || dst_size == 0) {
        return 0;
    }
    return snprintf(dst,
                    dst_size,
                    "\"controller\":{\"present\":%s,\"vid\":\"0x%04X\","
                    "\"pid\":\"0x%04X\",\"product\":\"%s\",\"midi_in\":%s,"
                    "\"midi_out\":%s,\"usb_audio\":%s,\"active_profile\":\"%s\","
                    "\"profiles\":%u}",
                    present ? "true" : "false",
                    vid & 0xFFFFu,
                    pid & 0xFFFFu,
                    product_escaped ? product_escaped : "",
                    midi_in ? "true" : "false",
                    midi_out ? "true" : "false",
                    usb_audio ? "true" : "false",
                    active_profile_escaped ? active_profile_escaped : "",
                    profile_count);
}

int web_api_alloc_printf(char **out, const char *fmt, ...)
{
    if (!out || !fmt) {
        return -1;
    }
    *out = NULL;

    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        va_end(args_copy);
        return -1;
    }

    char *buf = malloc((size_t)needed + 1u);
    if (!buf) {
        va_end(args_copy);
        return -1;
    }

    int written = vsnprintf(buf, (size_t)needed + 1u, fmt, args_copy);
    va_end(args_copy);
    if (written < 0 || written > needed) {
        free(buf);
        return -1;
    }

    *out = buf;
    return written;
}

uint32_t web_api_clamp_seek_ms(int value, uint32_t duration_ms, bool duration_known)
{
    if (value <= 0) {
        return 0u;
    }

    uint32_t pos_ms = (uint32_t)value;
    if (duration_known && duration_ms > 0u && pos_ms > duration_ms) {
        return duration_ms;
    }
    return pos_ms;
}
