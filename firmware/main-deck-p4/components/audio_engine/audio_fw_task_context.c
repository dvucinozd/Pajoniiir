#include "audio_fw_task_context.h"

#include <stddef.h>

void audio_fw_task_context_reset(audio_fw_task_context_t *ctx)
{
    if (!ctx) return;

    ctx->deck = 0;
    ctx->preload = NULL;
    ctx->runtime = NULL;
}

void audio_fw_task_context_bind(audio_fw_task_context_t *ctx,
                                uint8_t deck,
                                audio_fw_preload_t *preload,
                                audio_fw_runtime_t *runtime)
{
    if (!ctx) return;

    ctx->deck = deck;
    ctx->preload = preload;
    ctx->runtime = runtime;
}

bool audio_fw_task_context_is_bound(const audio_fw_task_context_t *ctx)
{
    return ctx && ctx->preload && ctx->runtime;
}
