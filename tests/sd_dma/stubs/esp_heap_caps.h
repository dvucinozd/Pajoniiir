#pragma once
#include <stddef.h>
#define MALLOC_CAP_DMA 1u
#define MALLOC_CAP_INTERNAL 2u
#define MALLOC_CAP_8BIT 4u
void *heap_caps_aligned_alloc(size_t, size_t, unsigned);
