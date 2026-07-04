#include "audio_fw_preload.h"
#include <stdio.h>

#define AUDIO_FW_PRELOAD_IDLE_CHUNK_BYTES   (256u * 1024u)
#define AUDIO_FW_PRELOAD_ACTIVE_CHUNK_BYTES (32u * 1024u)

void audio_fw_preload_begin_load(audio_fw_preload_t *slot)
{
    if (!slot) return;
    slot->buf = NULL;
    slot->loaded_bytes = 0u;
    slot->load_done = false;
}

void audio_fw_preload_reset(audio_fw_preload_t *slot)
{
    if (!slot) return;
    slot->path[0] = '\0';
    audio_fw_preload_begin_load(slot);
}

void audio_fw_preload_set_path(audio_fw_preload_t *slot, const char *path)
{
    if (!slot) return;
    snprintf(slot->path, sizeof(slot->path), "%s", path ? path : "");
}

void audio_fw_preload_abort_load(audio_fw_preload_t *slot, audio_fw_runtime_t *runtime)
{
    if (slot) {
        slot->load_done = true;
    }
    if (runtime) {
        runtime->run = false;
    }
}

size_t audio_fw_preload_chunk_bytes(size_t remaining_bytes, bool output_active)
{
    size_t cap = output_active
               ? AUDIO_FW_PRELOAD_ACTIVE_CHUNK_BYTES
               : AUDIO_FW_PRELOAD_IDLE_CHUNK_BYTES;
    return remaining_bytes < cap ? remaining_bytes : cap;
}
