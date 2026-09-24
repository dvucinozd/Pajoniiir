#include "p4_ota_pull_status.h"

#include <stdio.h>

void p4_ota_pull_format_failure_detail(char *out, size_t out_size,
                                       const char *detail,
                                       const char *error_name)
{
    if (!out || out_size == 0u) return;
    snprintf(out, out_size, "%s (%s)", detail ? detail : "",
             error_name ? error_name : "unknown error");
}
