#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM 0x400u
#define MALLOC_CAP_8BIT 0x4u
void *heap_caps_malloc(size_t, uint32_t);
void *heap_caps_realloc(void *, size_t, uint32_t);
void heap_caps_free(void *);
size_t heap_caps_get_total_size(uint32_t);
size_t heap_caps_get_allocated_size(void *);
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);
bool heap_caps_check_integrity(uint32_t, bool);
