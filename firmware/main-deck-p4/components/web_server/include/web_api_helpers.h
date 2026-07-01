#pragma once

#include <stddef.h>
#include <stdbool.h>

size_t web_api_json_escape(const char *src, char *dst, size_t dst_size);
int web_api_format_beat_fx_json(char *dst,
                                size_t dst_size,
                                int effect,
                                int beat,
                                int target,
                                unsigned depth,
                                bool enabled);
int web_api_format_beat_fx_echo_diag_json(char *dst,
                                          size_t dst_size,
                                          bool allocated1,
                                          bool allocated2,
                                          bool enabled1,
                                          bool enabled2,
                                          unsigned delay_ms1,
                                          unsigned delay_ms2);
