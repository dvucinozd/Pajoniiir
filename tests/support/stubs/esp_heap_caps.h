#pragma once

#include <stdint.h>
#include <stdlib.h>

/* All capability bits collapse to 0: the host has one heap, and no suite tests
 * placement. This is a level-1 stub, so allocation always behaves like the C
 * library. A suite that needs to drive an out-of-memory path deterministically
 * supplies its own esp_heap_caps.h ahead of this one on the include path — see
 * tests/support/README.md. */

#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT     0
#define MALLOC_CAP_32BIT    0
#define MALLOC_CAP_DMA      0
#define MALLOC_CAP_DEFAULT  0

static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    return calloc(n, size);
}

static inline void *heap_caps_realloc(void *p, size_t size, uint32_t caps)
{
    (void)caps;
    return realloc(p, size);
}

static inline void *heap_caps_aligned_alloc(size_t align, size_t size, uint32_t caps)
{
    (void)align;
    (void)caps;
    return malloc(size);
}

static inline void heap_caps_free(void *ptr)
{
    free(ptr);
}

static inline size_t heap_caps_get_free_size(uint32_t caps)
{
    (void)caps;
    return 1u << 20;
}

static inline size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return 1u << 20;
}
