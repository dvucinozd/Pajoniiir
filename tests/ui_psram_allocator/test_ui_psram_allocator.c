#include "lvgl.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static unsigned allocations, reallocations, frees;
static bool fail_next, intact = true;
static size_t total = 1024, available = 640, biggest = 320;
static void require_external(uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
void *heap_caps_malloc(size_t n, uint32_t caps)
{
    require_external(caps); ++allocations;
    if (fail_next) { fail_next = false; return NULL; }
    return malloc(n);
}
void *heap_caps_realloc(void *p, size_t n, uint32_t caps)
{
    require_external(caps); ++reallocations;
    if (fail_next) { fail_next = false; return NULL; }
    return realloc(p, n);
}
void heap_caps_free(void *p) { if (p) ++frees; free(p); }
size_t heap_caps_get_total_size(uint32_t caps) { require_external(caps); return total; }
size_t heap_caps_get_free_size(uint32_t caps) { require_external(caps); return available; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { require_external(caps); return biggest; }
bool heap_caps_check_integrity(uint32_t caps, bool print)
{
    require_external(caps); assert(!print); return intact;
}

int main(void)
{
    lv_mem_init();
    /* Even tiny labels must avoid the internal allocator threshold. */
    unsigned char *p = lv_malloc_core(8);
    assert(p); memset(p, 0x42, 8);
    p = lv_realloc_core(p, 300); assert(p);
    for (unsigned i = 0; i < 8; ++i) assert(p[i] == 0x42);
    fail_next = true;
    assert(lv_realloc_core(p, 600) == NULL);
    for (unsigned i = 0; i < 8; ++i) assert(p[i] == 0x42);
    assert(reallocations == 2 && frees == 0);
    lv_free_core(p); lv_free_core(NULL); assert(frees == 1);
    fail_next = true;
    assert(lv_malloc_core(16) == NULL);
    assert(allocations == 2); /* no internal retry/fallback on external OOM */
    p = lv_realloc_core(NULL, 32); assert(p); lv_free_core(p);
    lv_mem_monitor_t m; memset(&m, 0xff, sizeof m);
    lv_mem_monitor_core(&m);
    assert(m.total_size == 1024 && m.free_size == 640 && m.free_biggest_size == 320);
    assert(m.used_pct == 38 && m.frag_pct == 50);
    total = available = biggest = 0;
    lv_mem_monitor_core(&m); assert(m.used_pct == 0 && m.frag_pct == 0);
    assert(lv_mem_test_core() == LV_RESULT_OK);
    intact = false; assert(lv_mem_test_core() == LV_RESULT_INVALID);
    assert(lv_mem_add_pool(NULL, 0) == NULL); lv_mem_remove_pool(NULL);
    lv_mem_deinit();
    puts("PASS: external-only LVGL allocation, failure ownership, heap diagnostics");
    return 0;
}
