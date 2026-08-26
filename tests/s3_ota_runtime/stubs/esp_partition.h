#pragma once

#include <stddef.h>

typedef struct esp_partition_t {
    const char *label;
    size_t size;
} esp_partition_t;
