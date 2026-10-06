#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *loader_task;
    void *decode_task;
    void *output_task;
    volatile bool run;
    int tasks_started;
    bool codec_open;
    uint32_t session_generation;
} audio_fw_runtime_t;

void audio_fw_runtime_reset(audio_fw_runtime_t *runtime);
void audio_fw_runtime_begin_load(audio_fw_runtime_t *runtime);
void audio_fw_runtime_mark_task_started(audio_fw_runtime_t *runtime);
void audio_fw_runtime_mark_stopped(audio_fw_runtime_t *runtime);
void audio_fw_runtime_invalidate_session(audio_fw_runtime_t *runtime);
uint32_t audio_fw_runtime_session_generation(const audio_fw_runtime_t *runtime);

/* Called by the lifecycle owner after run admission is closed. Each successful
 * wait consumes exactly one worker acknowledgement, including across retries.
 * Keep context/cache/handles intact until this returns true. */
typedef bool (*audio_fw_wait_exit_fn)(void *ctx);
bool audio_fw_runtime_join(audio_fw_runtime_t *runtime,
                           audio_fw_wait_exit_fn wait_exit, void *ctx);
