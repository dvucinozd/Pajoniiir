#include "audio_compressed_cache.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t data[257];
    size_t size;
    uint32_t calls;
    /* Fault injection: while nonzero, the next `short_reads` backend transfers
     * deliver only `short_bytes` of whatever was asked for. */
    uint32_t short_reads;
    size_t short_bytes;
} memory_source_t;

static size_t memory_read_at(void *ctx, size_t offset, void *dst, size_t bytes)
{
    memory_source_t *source = (memory_source_t *)ctx;
    source->calls++;
    if (offset >= source->size) return 0u;
    if (bytes > source->size - offset) bytes = source->size - offset;
    if (source->short_reads > 0u) {
        source->short_reads--;
        if (bytes > source->short_bytes) bytes = source->short_bytes;
    }
    memcpy(dst, source->data + offset, bytes);
    return bytes;
}

static void fill_source(memory_source_t *source)
{
    memset(source, 0, sizeof(*source));
    source->size = sizeof(source->data);
    source->calls = 0u;
    for (size_t i = 0; i < source->size; ++i) source->data[i] = (uint8_t)(i ^ 0x5Au);
}

static void test_cross_page_read_and_eof_clamp(void)
{
    memory_source_t source;
    fill_source(&source);
    uint8_t storage[4 * 32];
    audio_compressed_cache_t cache;
    assert(audio_compressed_cache_init(&cache, storage, sizeof(storage), 32u,
                                       source.size, memory_read_at, &source));

    uint8_t out[70];
    assert(audio_compressed_cache_read(&cache, 17u, out, sizeof(out)) == sizeof(out));
    assert(memcmp(out, source.data + 17u, sizeof(out)) == 0);
    assert(cache.misses == 3u);
    assert(source.calls == 3u);

    uint8_t tail[32];
    assert(audio_compressed_cache_read(&cache, 250u, tail, sizeof(tail)) == 7u);
    assert(memcmp(tail, source.data + 250u, 7u) == 0);
}

static void test_hits_and_lru_eviction_stay_bounded(void)
{
    memory_source_t source;
    fill_source(&source);
    uint8_t storage[2 * 32];
    audio_compressed_cache_t cache;
    assert(audio_compressed_cache_init(&cache, storage, sizeof(storage), 32u,
                                       source.size, memory_read_at, &source));

    uint8_t out[8];
    assert(audio_compressed_cache_read(&cache, 0u, out, sizeof(out)) == sizeof(out));
    assert(audio_compressed_cache_read(&cache, 40u, out, sizeof(out)) == sizeof(out));
    assert(audio_compressed_cache_read(&cache, 1u, out, sizeof(out)) == sizeof(out));
    assert(cache.hits == 1u);
    assert(source.calls == 2u);

    assert(audio_compressed_cache_read(&cache, 80u, out, sizeof(out)) == sizeof(out));
    assert(source.calls == 3u);
    assert(audio_compressed_cache_read(&cache, 40u, out, sizeof(out)) == sizeof(out));
    assert(source.calls == 4u);
    assert(audio_compressed_cache_capacity(&cache) == sizeof(storage));
}

static void test_prefetch_and_invalid_config(void)
{
    memory_source_t source;
    fill_source(&source);
    uint8_t storage[64];
    audio_compressed_cache_t cache;
    assert(!audio_compressed_cache_init(&cache, storage, 8u, 16u,
                                        source.size, memory_read_at, &source));
    assert(audio_compressed_cache_init(&cache, storage, sizeof(storage), 16u,
                                       source.size, memory_read_at, &source));
    assert(audio_compressed_cache_prefetch(&cache, 33u));
    assert(source.calls == 1u);
    assert(audio_compressed_cache_prefetch(&cache, 47u));
    assert(source.calls == 1u);
    assert(!audio_compressed_cache_prefetch(&cache, source.size));
}

/* A backend fault that truncates one transfer must never become a cached page.
 * Before this was enforced, the short extent was published as valid and every
 * later hit returned the truncated read, so the decoder saw a permanent
 * premature EOF that survived until LRU happened to evict the slot. */
static void test_mid_file_short_read_is_not_cached(void)
{
    memory_source_t source;
    fill_source(&source);
    uint8_t storage[4 * 32];
    audio_compressed_cache_t cache;
    assert(audio_compressed_cache_init(&cache, storage, sizeof(storage), 32u,
                                       source.size, memory_read_at, &source));

    /* First transfer of page 0 delivers 8 of the 32 requested bytes. */
    source.short_reads = 1u;
    source.short_bytes = 8u;

    uint8_t out[32];
    memset(out, 0xA5, sizeof(out));
    assert(audio_compressed_cache_read(&cache, 0u, out, sizeof(out)) == 0u);
    assert(cache.short_reads == 1u);
    assert(cache.hits == 0u);

    /* The faulted slot must not answer the retry from cache. */
    memset(out, 0xA5, sizeof(out));
    assert(audio_compressed_cache_read(&cache, 0u, out, sizeof(out)) == sizeof(out));
    assert(memcmp(out, source.data, sizeof(out)) == 0);
    assert(cache.hits == 0u);
    assert(cache.short_reads == 1u);

    /* And the recovered page then serves hits normally. */
    uint8_t again[16];
    assert(audio_compressed_cache_read(&cache, 4u, again, sizeof(again)) == sizeof(again));
    assert(memcmp(again, source.data + 4u, sizeof(again)) == 0);
    assert(cache.hits == 1u);
}

/* A short read on the final page is real EOF, not a fault: `wanted` is clamped
 * to the file tail, so the transfer is complete and the page stays cacheable. */
static void test_eof_tail_page_is_cached(void)
{
    memory_source_t source;
    fill_source(&source);
    uint8_t storage[4 * 32];
    audio_compressed_cache_t cache;
    assert(audio_compressed_cache_init(&cache, storage, sizeof(storage), 32u,
                                       source.size, memory_read_at, &source));

    /* 257 bytes over 32-byte pages: the last page holds a single byte. */
    uint8_t tail[32];
    assert(audio_compressed_cache_read(&cache, 256u, tail, sizeof(tail)) == 1u);
    assert(tail[0] == source.data[256]);
    assert(cache.short_reads == 0u);
    assert(cache.misses == 1u);

    /* The one-byte tail page is cacheable and answers the repeat as a hit. */
    assert(audio_compressed_cache_read(&cache, 256u, tail, sizeof(tail)) == 1u);
    assert(cache.hits == 1u);
    assert(cache.misses == 1u);
    assert(cache.short_reads == 0u);
}

int main(void)
{
    test_cross_page_read_and_eof_clamp();
    test_hits_and_lru_eviction_stay_bounded();
    test_prefetch_and_invalid_config();
    test_mid_file_short_read_is_not_cached();
    test_eof_tail_page_is_cached();
    puts("audio_compressed_cache tests passed");
    return 0;
}
