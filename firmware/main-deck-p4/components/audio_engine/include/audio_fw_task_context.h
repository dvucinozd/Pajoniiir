#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_fw_preload.h"
#include "audio_fw_runtime.h"
#include "audio_fw_task_plan.h"

typedef struct {
    uint8_t deck;
    audio_fw_preload_t *preload;
    audio_fw_runtime_t *runtime;
    void *engine;
    void *pcm_ring;
    void *resampler;
    audio_fw_task_plan_t task_plan;
    uint32_t session_generation;
} audio_fw_task_context_t;

void audio_fw_task_context_reset(audio_fw_task_context_t *ctx);
void audio_fw_task_context_bind(audio_fw_task_context_t *ctx,
                                uint8_t deck,
                                audio_fw_preload_t *preload,
                                audio_fw_runtime_t *runtime,
                                void *engine,
                                void *pcm_ring,
                                void *resampler,
                                audio_fw_task_plan_t task_plan);
bool audio_fw_task_context_is_bound(const audio_fw_task_context_t *ctx);
bool audio_fw_task_context_is_current(const audio_fw_task_context_t *ctx);
