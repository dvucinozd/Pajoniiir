#include "audio_fw_preload.h"

#include <stdio.h>
#include <string.h>

void audio_fw_preload_begin_load(audio_fw_preload_t *slot)
{
    if (!slot) return;
    slot->buf = NULL;
    slot->buf_size = 0u;
    slot->file_size = 0u;
    slot->source = NULL;
    audio_compressed_cache_reset(&slot->cache);
    slot->stream_pos = 0u;
    slot->loaded_bytes = 0u;
    slot->load_done = false;
}

void audio_fw_preload_reset(audio_fw_preload_t *slot)
{
    if (!slot) return;
    memset(slot, 0, sizeof(*slot));
}

void audio_fw_preload_set_path(audio_fw_preload_t *slot, const char *path)
{
    if (!slot) return;
    snprintf(slot->path, sizeof(slot->path), "%s", path ? path : "");
    slot->path[sizeof(slot->path) - 1u] = '\0';
}

void audio_fw_preload_abort_load(audio_fw_preload_t *slot, audio_fw_runtime_t *runtime)
{
    if (slot) slot->load_done = true;
    if (runtime) runtime->run = false;
}

bool audio_fw_preload_bind_cache(audio_fw_preload_t *slot,
                                 uint8_t *storage,
                                 size_t storage_bytes,
                                 size_t file_size,
                                 void *source,
                                 audio_compressed_cache_read_at_fn read_at)
{
    if (!slot || !storage || !source || !read_at || file_size == 0u) return false;
    if (!audio_compressed_cache_init(&slot->cache,
                                     storage,
                                     storage_bytes,
                                     AUDIO_FW_CACHE_PAGE_BYTES,
                                     file_size,
                                     read_at,
                                     slot)) {
        return false;
    }
    slot->buf = storage;
    slot->buf_size = audio_compressed_cache_capacity(&slot->cache);
    slot->file_size = file_size;
    slot->source = source;
    slot->stream_pos = 0u;
    slot->loaded_bytes = 0u;
    slot->load_done = false;
    return true;
}

size_t audio_fw_preload_read_at(audio_fw_preload_t *slot,
                                size_t offset,
                                void *dst,
                                size_t bytes)
{
    if (!slot) return 0u;
    size_t got = audio_compressed_cache_read(&slot->cache, offset, dst, bytes);
    slot->loaded_bytes = slot->cache.backend_bytes;
    return got;
}

size_t audio_fw_preload_stream_read(audio_fw_preload_t *slot,
                                    void *dst,
                                    size_t bytes)
{
    if (!slot) return 0u;
    size_t got = audio_fw_preload_read_at(slot, slot->stream_pos, dst, bytes);
    slot->stream_pos += got;
    return got;
}

bool audio_fw_preload_stream_seek(audio_fw_preload_t *slot,
                                  int64_t offset,
                                  int origin)
{
    if (!slot) return false;
    int64_t base = 0;
    if (origin == SEEK_CUR) {
        base = (int64_t)slot->stream_pos;
    } else if (origin == SEEK_END) {
        base = (int64_t)slot->file_size;
    } else if (origin != SEEK_SET) {
        return false;
    }
    int64_t target = base + offset;
    if (target < 0 || (uint64_t)target > (uint64_t)slot->file_size) return false;
    slot->stream_pos = (size_t)target;
    return true;
}

size_t audio_fw_preload_stream_tell(const audio_fw_preload_t *slot)
{
    return slot ? slot->stream_pos : 0u;
}
