#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_compressed_cache.h"
#include "audio_fw_runtime.h"

#define AUDIO_FW_PRELOAD_PATH_LEN 384u
#define AUDIO_FW_CACHE_PAGE_BYTES (32u * 1024u)
#define AUDIO_FW_CACHE_PAGE_COUNT  8u
#define AUDIO_FW_CACHE_BYTES       (AUDIO_FW_CACHE_PAGE_BYTES * AUDIO_FW_CACHE_PAGE_COUNT)

typedef struct {
    char path[AUDIO_FW_PRELOAD_PATH_LEN];
    uint8_t *buf;
    size_t buf_size;
    size_t file_size;
    void *source;
    audio_compressed_cache_t cache;
    size_t stream_pos;
    /* Monotonic callback fault marker. A short stream read before the declared
     * file end increments this so decoders can distinguish media I/O failure
     * from a legitimate EOF even when their API returns only a byte count. */
    uint32_t stream_fault_epoch;
    size_t stream_fault_offset;
    volatile size_t loaded_bytes;
    volatile bool load_done;
} audio_fw_preload_t;

void audio_fw_preload_reset(audio_fw_preload_t *slot);
void audio_fw_preload_begin_load(audio_fw_preload_t *slot);
void audio_fw_preload_set_path(audio_fw_preload_t *slot, const char *path);
void audio_fw_preload_abort_load(audio_fw_preload_t *slot, audio_fw_runtime_t *runtime);
bool audio_fw_preload_bind_cache(audio_fw_preload_t *slot,
                                 uint8_t *storage,
                                 size_t storage_bytes,
                                 size_t file_size,
                                 void *source,
                                 audio_compressed_cache_read_at_fn read_at);
size_t audio_fw_preload_read_at(audio_fw_preload_t *slot,
                                size_t offset,
                                void *dst,
                                size_t bytes);
size_t audio_fw_preload_stream_read(audio_fw_preload_t *slot,
                                    void *dst,
                                    size_t bytes);
bool audio_fw_preload_stream_seek(audio_fw_preload_t *slot,
                                  int64_t offset,
                                  int origin);
size_t audio_fw_preload_stream_tell(const audio_fw_preload_t *slot);
uint32_t audio_fw_preload_stream_fault_epoch(const audio_fw_preload_t *slot);
