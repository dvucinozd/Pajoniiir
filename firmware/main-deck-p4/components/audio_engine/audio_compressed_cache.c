#include "audio_compressed_cache.h"

#include <string.h>

static size_t page_slot_offset(const audio_compressed_cache_t *cache,
                               size_t slot)
{
    return slot * cache->page_size;
}

static size_t page_aligned_offset(const audio_compressed_cache_t *cache,
                                  size_t offset)
{
    return (offset / cache->page_size) * cache->page_size;
}

static audio_compressed_cache_page_t *find_page(audio_compressed_cache_t *cache,
                                                 size_t aligned_offset,
                                                 size_t *out_slot)
{
    for (size_t i = 0; i < cache->page_count; ++i) {
        audio_compressed_cache_page_t *page = &cache->pages[i];
        if (page->valid && page->offset == aligned_offset) {
            if (out_slot) *out_slot = i;
            return page;
        }
    }
    return NULL;
}

static size_t choose_victim(const audio_compressed_cache_t *cache)
{
    size_t victim = 0u;
    uint64_t oldest = UINT64_MAX;
    for (size_t i = 0; i < cache->page_count; ++i) {
        const audio_compressed_cache_page_t *page = &cache->pages[i];
        if (!page->valid) return i;
        if (page->stamp < oldest) {
            oldest = page->stamp;
            victim = i;
        }
    }
    return victim;
}

static audio_compressed_cache_page_t *load_page(audio_compressed_cache_t *cache,
                                                 size_t aligned_offset,
                                                 size_t *out_slot)
{
    if (!cache || !cache->read_at || aligned_offset >= cache->file_size) {
        return NULL;
    }
    size_t slot = choose_victim(cache);
    uint8_t *dst = cache->storage + page_slot_offset(cache, slot);
    size_t wanted = cache->page_size;
    if (wanted > cache->file_size - aligned_offset) {
        wanted = cache->file_size - aligned_offset;
    }
    size_t got = cache->read_at(cache->read_ctx, aligned_offset, dst, wanted);
    cache->misses++;
    cache->backend_bytes += got;
    audio_compressed_cache_page_t *page = &cache->pages[slot];
    if (got != wanted) {
        /* `wanted` is already clamped to the file tail, so a short read here is a
         * backend fault (media gate closed, USB glitch, FATFS error), not EOF.
         * Publishing it would make every later hit on this page return a
         * truncated extent, which the decoder reads as a permanent premature EOF
         * until LRU happens to evict the slot. Retire the slot — its storage is
         * clobbered either way — so the next read retries the transfer. */
        page->valid = false;
        page->valid_bytes = 0u;
        page->offset = 0u;
        page->stamp = 0u;
        cache->short_reads++;
        if (out_slot) *out_slot = slot;
        return NULL;
    }
    page->offset = aligned_offset;
    page->valid_bytes = got;
    page->stamp = ++cache->stamp;
    page->valid = true;
    if (out_slot) *out_slot = slot;
    return page;
}

bool audio_compressed_cache_init(audio_compressed_cache_t *cache,
                                 uint8_t *storage,
                                 size_t storage_bytes,
                                 size_t page_size,
                                 size_t file_size,
                                 audio_compressed_cache_read_at_fn read_at,
                                 void *read_ctx)
{
    if (!cache || !storage || page_size == 0u || storage_bytes < page_size ||
        !read_at || file_size == 0u) {
        return false;
    }
    size_t page_count = storage_bytes / page_size;
    if (page_count == 0u || page_count > AUDIO_COMPRESSED_CACHE_MAX_PAGES) {
        return false;
    }
    memset(cache, 0, sizeof(*cache));
    cache->storage = storage;
    cache->storage_bytes = page_count * page_size;
    cache->page_size = page_size;
    cache->page_count = page_count;
    cache->file_size = file_size;
    cache->read_at = read_at;
    cache->read_ctx = read_ctx;
    return true;
}

void audio_compressed_cache_reset(audio_compressed_cache_t *cache)
{
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
}

size_t audio_compressed_cache_read(audio_compressed_cache_t *cache,
                                   size_t offset,
                                   void *dst,
                                   size_t bytes)
{
    if (!cache || !dst || bytes == 0u || offset >= cache->file_size) return 0u;
    if (bytes > cache->file_size - offset) bytes = cache->file_size - offset;

    uint8_t *out = (uint8_t *)dst;
    size_t copied = 0u;
    while (copied < bytes) {
        size_t absolute = offset + copied;
        size_t aligned = page_aligned_offset(cache, absolute);
        size_t slot = 0u;
        audio_compressed_cache_page_t *page = find_page(cache, aligned, &slot);
        if (page) {
            cache->hits++;
            page->stamp = ++cache->stamp;
        } else {
            page = load_page(cache, aligned, &slot);
            if (!page) break;
        }
        size_t within = absolute - aligned;
        if (within >= page->valid_bytes) break;
        size_t available = page->valid_bytes - within;
        size_t take = bytes - copied;
        if (take > available) take = available;
        memcpy(out + copied,
               cache->storage + page_slot_offset(cache, slot) + within,
               take);
        copied += take;
        if (take == 0u) break;
    }
    return copied;
}

bool audio_compressed_cache_prefetch(audio_compressed_cache_t *cache,
                                     size_t offset)
{
    if (!cache || offset >= cache->file_size) return false;
    size_t aligned = page_aligned_offset(cache, offset);
    if (find_page(cache, aligned, NULL)) return true;
    return load_page(cache, aligned, NULL) != NULL;
}

size_t audio_compressed_cache_capacity(const audio_compressed_cache_t *cache)
{
    return cache ? cache->storage_bytes : 0u;
}
