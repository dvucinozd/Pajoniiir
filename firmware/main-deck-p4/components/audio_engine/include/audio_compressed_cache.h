#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_COMPRESSED_CACHE_MAX_PAGES 8u

typedef size_t (*audio_compressed_cache_read_at_fn)(void *ctx,
                                                    size_t offset,
                                                    void *dst,
                                                    size_t bytes);

typedef struct {
    size_t offset;
    size_t valid_bytes;
    uint64_t stamp;
    bool valid;
} audio_compressed_cache_page_t;

typedef struct {
    uint8_t *storage;
    size_t storage_bytes;
    size_t page_size;
    size_t page_count;
    size_t file_size;
    audio_compressed_cache_read_at_fn read_at;
    void *read_ctx;
    audio_compressed_cache_page_t pages[AUDIO_COMPRESSED_CACHE_MAX_PAGES];
    uint64_t stamp;
    size_t backend_bytes;
    uint32_t hits;
    uint32_t misses;
    uint32_t short_reads;  /* backend returned less than the clamped page extent */
} audio_compressed_cache_t;

bool audio_compressed_cache_init(audio_compressed_cache_t *cache,
                                 uint8_t *storage,
                                 size_t storage_bytes,
                                 size_t page_size,
                                 size_t file_size,
                                 audio_compressed_cache_read_at_fn read_at,
                                 void *read_ctx);
void audio_compressed_cache_reset(audio_compressed_cache_t *cache);
size_t audio_compressed_cache_read(audio_compressed_cache_t *cache,
                                   size_t offset,
                                   void *dst,
                                   size_t bytes);
bool audio_compressed_cache_prefetch(audio_compressed_cache_t *cache,
                                     size_t offset);
size_t audio_compressed_cache_capacity(const audio_compressed_cache_t *cache);
