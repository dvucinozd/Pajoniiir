#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_FW_PRELOAD_PATH_LEN 384u

typedef struct {
    char path[AUDIO_FW_PRELOAD_PATH_LEN];
    uint8_t *buf;
    volatile size_t loaded_bytes;
    volatile bool load_done;
} audio_fw_preload_t;

void audio_fw_preload_reset(audio_fw_preload_t *slot);
void audio_fw_preload_begin_load(audio_fw_preload_t *slot);
void audio_fw_preload_set_path(audio_fw_preload_t *slot, const char *path);
