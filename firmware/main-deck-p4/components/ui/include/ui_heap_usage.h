#pragma once
#include <stdint.h>
/* UINT32_MAX means regular libc LVGL allocator has no separate accounting. */
uint32_t ui_heap_usage_bytes(void);
