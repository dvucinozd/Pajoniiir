#include "audio_fw_preload.h"
#include <stdio.h>

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
