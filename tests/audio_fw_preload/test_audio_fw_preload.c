#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t data[256];
} source_t;

static size_t read_at(void *ctx, size_t offset, void *dst, size_t bytes)
{
    audio_fw_preload_t *slot = (audio_fw_preload_t *)ctx;
    source_t *source = (source_t *)slot->source;
    if (offset >= sizeof(source->data)) return 0u;
    if (bytes > sizeof(source->data) - offset) bytes = sizeof(source->data) - offset;
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
    source_t source;
    for (size_t i = 0; i < sizeof(source.data); ++i) source.data[i] = (uint8_t)i;
    uint8_t storage[AUDIO_FW_CACHE_PAGE_BYTES];
    audio_fw_preload_t slot;
    audio_fw_preload_reset(&slot);
    assert(audio_fw_preload_bind_cache(&slot, storage, sizeof(storage),
                                       sizeof(source.data), &source, read_at));
    assert(slot.buf_size == sizeof(storage));
    assert(slot.file_size == sizeof(source.data));

    uint8_t out[12];
    assert(audio_fw_preload_read_at(&slot, 20u, out, sizeof(out)) == sizeof(out));
    assert(memcmp(out, source.data + 20u, sizeof(out)) == 0);
    assert(slot.loaded_bytes == sizeof(source.data));

    assert(audio_fw_preload_stream_seek(&slot, 100, SEEK_SET));
    assert(audio_fw_preload_stream_read(&slot, out, 8u) == 8u);
    assert(memcmp(out, source.data + 100u, 8u) == 0);
    assert(audio_fw_preload_stream_tell(&slot) == 108u);
    assert(audio_fw_preload_stream_seek(&slot, -8, SEEK_CUR));
    assert(audio_fw_preload_stream_tell(&slot) == 100u);
    assert(!audio_fw_preload_stream_seek(&slot, 1, SEEK_END));
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
    test_abort_load_wakes_waiters_and_stops_runtime();
    puts("audio_fw_preload tests passed");
    return 0;
}
