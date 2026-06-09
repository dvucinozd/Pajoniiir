#include "audio_fw_runtime.h"

#include <stddef.h>

void audio_fw_runtime_reset(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;

    runtime->loader_task = NULL;
    runtime->decode_task = NULL;
    runtime->output_task = NULL;
    runtime->run = false;
    runtime->tasks_started = 0;
    runtime->codec_open = false;
}

void audio_fw_runtime_begin_load(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;

    audio_fw_runtime_reset(runtime);
    runtime->run = true;
}

void audio_fw_runtime_mark_task_started(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;

    runtime->tasks_started++;
}

void audio_fw_runtime_mark_stopped(audio_fw_runtime_t *runtime)
{
    audio_fw_runtime_reset(runtime);
}
