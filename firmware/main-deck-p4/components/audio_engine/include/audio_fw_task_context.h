#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"

typedef struct {
    uint8_t deck;
    audio_fw_preload_t *preload;
    audio_fw_runtime_t *runtime;
} audio_fw_task_context_t;

void audio_fw_task_context_reset(audio_fw_task_context_t *ctx);
void audio_fw_task_context_bind(audio_fw_task_context_t *ctx,
                                uint8_t deck,
                                audio_fw_preload_t *preload,
                                audio_fw_runtime_t *runtime);
bool audio_fw_task_context_is_bound(const audio_fw_task_context_t *ctx);
