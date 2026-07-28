#include "audio_compressed_cache.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t data[257];
    size_t size;
    uint32_t calls;
} memory_source_t;

static size_t memory_read_at(void *ctx, size_t offset, void *dst, size_t bytes)
{
    memory_source_t *source = (memory_source_t *)ctx;
    source->calls++;
    if (offset >= source->size) return 0u;
    if (bytes > source->size - offset) bytes = source->size - offset;
    memcpy(dst, source->data + offset, bytes);
    return bytes;
}

static void fill_source(memory_source_t *source)
{
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

int main(void)
{
    test_cross_page_read_and_eof_clamp();
    test_hits_and_lru_eviction_stay_bounded();
    test_prefetch_and_invalid_config();
    puts("audio_compressed_cache tests passed");
    return 0;
}
