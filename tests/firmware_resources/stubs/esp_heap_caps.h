#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define MALLOC_CAP_SPIRAM (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
typedef void (*heap_caps_alloc_failed_hook_t)(size_t, uint32_t, const char *);
esp_err_t heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook_t callback);
