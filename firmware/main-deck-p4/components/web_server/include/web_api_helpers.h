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
