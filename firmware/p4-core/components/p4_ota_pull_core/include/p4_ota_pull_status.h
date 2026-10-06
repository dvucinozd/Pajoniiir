#pragma once

#include <stddef.h>

/* Formats the operator-facing failed-check detail without coupling host tests
 * to app_main or ESP-IDF. */
void p4_ota_pull_format_failure_detail(char *out, size_t out_size,
                                       const char *detail,
                                       const char *error_name);
