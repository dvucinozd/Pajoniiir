#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t data[AUDIO_FW_CACHE_PAGE_BYTES * 2u];
    uint32_t short_reads;
} source_t;

static size_t read_at(void *ctx, size_t offset, void *dst, size_t bytes)
{
    audio_fw_preload_t *slot = (audio_fw_preload_t *)ctx;
    source_t *source = (source_t *)slot->source;
    if (offset >= sizeof(source->data)) return 0u;
    if (bytes > sizeof(source->data) - offset) bytes = sizeof(source->data) - offset;
    if (source->short_reads > 0u) {
        source->short_reads--;
        return bytes > 1u ? bytes - 1u : 0u;
    }
    memcpy(dst, source->data + offset, bytes);
    return bytes;
}

static void test_reset_clears_transient_state(void)
{
    audio_fw_preload_t slot;
    memset(&slot, 0xA5, sizeof(slot));
    strcpy(slot.path, "/usb/old.mp3");
    audio_fw_preload_reset(&slot);
    assert(slot.path[0] == '\0');
    assert(slot.buf == NULL);
    assert(slot.buf_size == 0u);
    assert(slot.file_size == 0u);
    assert(slot.source == NULL);
    assert(slot.loaded_bytes == 0u);
    assert(!slot.load_done);
}

static void test_set_path_copies_and_terminates(void)
{
    audio_fw_preload_t slot;
    audio_fw_preload_reset(&slot);
    audio_fw_preload_set_path(&slot, "/usb/Music/track.mp3");
    assert(strcmp(slot.path, "/usb/Music/track.mp3") == 0);
    assert(slot.path[AUDIO_FW_PRELOAD_PATH_LEN - 1u] == '\0');
}

static void test_bounded_cache_and_seekable_stream(void)
{
    static source_t source;
    for (size_t i = 0; i < sizeof(source.data); ++i) source.data[i] = (uint8_t)i;
    static uint8_t storage[AUDIO_FW_CACHE_PAGE_BYTES];
    audio_fw_preload_t slot;
    audio_fw_preload_reset(&slot);
    assert(audio_fw_preload_bind_cache(&slot, storage, sizeof(storage),
                                       sizeof(source.data), &source, read_at));
    assert(slot.buf_size == sizeof(storage));
    assert(slot.file_size == sizeof(source.data));

    uint8_t out[12];
    assert(audio_fw_preload_read_at(&slot, 20u, out, sizeof(out)) == sizeof(out));
    assert(memcmp(out, source.data + 20u, sizeof(out)) == 0);
    assert(slot.loaded_bytes == AUDIO_FW_CACHE_PAGE_BYTES);

    assert(audio_fw_preload_stream_seek(&slot, 100, SEEK_SET));
    assert(audio_fw_preload_stream_read(&slot, out, 8u) == 8u);
    assert(memcmp(out, source.data + 100u, 8u) == 0);
    assert(audio_fw_preload_stream_tell(&slot) == 108u);
    assert(audio_fw_preload_stream_seek(&slot, -8, SEEK_CUR));
    assert(audio_fw_preload_stream_tell(&slot) == 100u);
    assert(!audio_fw_preload_stream_seek(&slot, 1, SEEK_END));
    assert(audio_fw_preload_stream_fault_epoch(&slot) == 0u);
}

static void test_stream_short_read_before_eof_publishes_fault_and_retries(void)
{
    static source_t source;
    static uint8_t storage[AUDIO_FW_CACHE_PAGE_BYTES * 2u];
    for (size_t i = 0; i < sizeof(source.data); ++i) {
        source.data[i] = (uint8_t)(i & 0xFFu);
    }
    source.short_reads = 0u;
    audio_fw_preload_t slot;
    audio_fw_preload_reset(&slot);
    assert(audio_fw_preload_bind_cache(&slot, storage, sizeof(storage),
                                       sizeof(source.data), &source, read_at));

    /* Warm the final bytes of page 0, then fail page 1. The stream callback
     * returns a partial prefix, advances only by that prefix and publishes a
     * fault instead of presenting the page boundary as EOF. */
    uint8_t warm[4];
    size_t boundary = AUDIO_FW_CACHE_PAGE_BYTES;
    assert(audio_fw_preload_read_at(&slot, boundary - 4u,
                                    warm, sizeof(warm)) == sizeof(warm));
    assert(audio_fw_preload_stream_seek(&slot, (int64_t)boundary - 4,
                                        SEEK_SET));
    source.short_reads = 1u;
    uint8_t out[8] = { 0 };
    assert(audio_fw_preload_stream_read(&slot, out, sizeof(out)) == 4u);
    assert(audio_fw_preload_stream_tell(&slot) == boundary);
    assert(audio_fw_preload_stream_fault_epoch(&slot) == 1u);
    assert(slot.stream_fault_offset == boundary);

    /* The failed cache page was not published. A later decoder retry reads it
     * again successfully while the fault epoch remains a durable marker. */
    assert(audio_fw_preload_stream_read(&slot, out + 4u, 4u) == 4u);
    assert(audio_fw_preload_stream_tell(&slot) == boundary + 4u);
    assert(audio_fw_preload_stream_fault_epoch(&slot) == 1u);
    assert(memcmp(out, source.data + boundary - 4u, sizeof(out)) == 0);

    /* A zero-byte read at the declared file end is legitimate EOF. */
    assert(audio_fw_preload_stream_seek(&slot, 0, SEEK_END));
    assert(audio_fw_preload_stream_read(&slot, out, sizeof(out)) == 0u);
    assert(audio_fw_preload_stream_fault_epoch(&slot) == 1u);
}

static void test_abort_load_wakes_waiters_and_stops_runtime(void)
{
    audio_fw_preload_t slot;
    audio_fw_runtime_t runtime;
    audio_fw_preload_reset(&slot);
    audio_fw_runtime_begin_load(&runtime);
    slot.load_done = false;
    audio_fw_preload_abort_load(&slot, &runtime);
    assert(slot.load_done);
    assert(!runtime.run);
}

int main(void)
{
    test_reset_clears_transient_state();
    test_set_path_copies_and_terminates();
    test_bounded_cache_and_seekable_stream();
    test_stream_short_read_before_eof_publishes_fault_and_retries();
    test_abort_load_wakes_waiters_and_stops_runtime();
    puts("audio_fw_preload tests passed");
    return 0;
}
