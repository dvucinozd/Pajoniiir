#pragma once
#include <stddef.h>
#include <stdint.h>
typedef void *lv_mem_pool_t;
typedef enum { LV_RESULT_INVALID, LV_RESULT_OK } lv_result_t;
typedef struct {
    size_t total_size, free_size, free_biggest_size;
    uint8_t used_pct, frag_pct;
} lv_mem_monitor_t;
void lv_mem_init(void);
void lv_mem_deinit(void);
lv_mem_pool_t lv_mem_add_pool(void *, size_t);
void lv_mem_remove_pool(lv_mem_pool_t);
void *lv_malloc_core(size_t);
void *lv_realloc_core(void *, size_t);
void lv_free_core(void *);
void lv_mem_monitor_core(lv_mem_monitor_t *);
lv_result_t lv_mem_test_core(void);
