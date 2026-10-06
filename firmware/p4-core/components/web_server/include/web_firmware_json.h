#pragma once

#include <stddef.h>

/* Escape one NUL-terminated string in place for safe insertion into a JSON
 * string literal. The destination capacity is unchanged; when expansion would
 * exceed it the result is truncated only at a complete escape-sequence boundary. */
void web_firmware_json_escape_in_place(char *value, size_t value_size);
