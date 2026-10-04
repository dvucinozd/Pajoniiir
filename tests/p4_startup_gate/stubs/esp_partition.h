#pragma once
#include <stdint.h>
typedef struct {
    const char *label;
    uint32_t address;
    uint32_t size;
} esp_partition_t;
