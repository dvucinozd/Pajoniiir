#include "audio_fw_runtime.h"

#include <stddef.h>

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation == 0u ? 1u : generation;
}

static void clear_owned_state(audio_fw_runtime_t *runtime)
{
    runtime->loader_task = NULL;
    runtime->decode_task = NULL;
    runtime->output_task = NULL;
    runtime->run = false;
    runtime->tasks_started = 0;
    runtime->codec_open = false;
}

void audio_fw_runtime_reset(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;

    __atomic_store_n(&runtime->session_generation, 0u, __ATOMIC_RELEASE);
    clear_owned_state(runtime);
}

void audio_fw_runtime_begin_load(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;

    uint32_t generation = audio_fw_runtime_session_generation(runtime);
    __atomic_store_n(&runtime->session_generation, next_generation(generation),
                     __ATOMIC_RELEASE);
    clear_owned_state(runtime);
    runtime->run = true;
}

void audio_fw_runtime_mark_task_started(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;

    runtime->tasks_started++;
}

void audio_fw_runtime_mark_stopped(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;
    clear_owned_state(runtime);
}

void audio_fw_runtime_invalidate_session(audio_fw_runtime_t *runtime)
{
    if (!runtime) return;
    uint32_t generation = audio_fw_runtime_session_generation(runtime);
    __atomic_store_n(&runtime->session_generation, next_generation(generation),
                     __ATOMIC_RELEASE);
    runtime->run = false;
}

uint32_t audio_fw_runtime_session_generation(const audio_fw_runtime_t *runtime)
{
    return runtime ? __atomic_load_n(&runtime->session_generation,
                                     __ATOMIC_ACQUIRE) : 0u;
}
