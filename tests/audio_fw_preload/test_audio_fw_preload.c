#include "audio_fw_preload.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_reset_clears_transient_state(void)
{
    audio_fw_preload_t slot = {
        .buf = (uint8_t *)0x1234,
        .loaded_bytes = 55,
        .load_done = true,
    };
    strcpy(slot.path, "/usb/old.mp3");

    audio_fw_preload_reset(&slot);

    assert(slot.path[0] == '\0');
    assert(slot.buf == NULL);
    assert(slot.loaded_bytes == 0);
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

static void test_begin_load_keeps_path_but_clears_progress(void)
{
    audio_fw_preload_t slot = {
        .buf = (uint8_t *)0x1234,
        .loaded_bytes = 55,
        .load_done = true,
    };
    strcpy(slot.path, "/usb/current.mp3");

    audio_fw_preload_begin_load(&slot);

    assert(strcmp(slot.path, "/usb/current.mp3") == 0);
    assert(slot.buf == NULL);
    assert(slot.loaded_bytes == 0);
    assert(!slot.load_done);
}

static void test_chunk_size_is_smaller_when_output_is_active(void)
{
    assert(audio_fw_preload_chunk_bytes(1024u * 1024u, false) == 256u * 1024u);
    assert(audio_fw_preload_chunk_bytes(1024u * 1024u, true) == 32u * 1024u);
    assert(audio_fw_preload_chunk_bytes(4096u, false) == 4096u);
    assert(audio_fw_preload_chunk_bytes(4096u, true) == 4096u);
    assert(audio_fw_preload_chunk_bytes(0u, true) == 0u);
}

int main(void)
{
    test_reset_clears_transient_state();
    test_set_path_copies_and_terminates();
    test_begin_load_keeps_path_but_clears_progress();
    test_chunk_size_is_smaller_when_output_is_active();
    puts("audio_fw_preload tests passed");
    return 0;
}
