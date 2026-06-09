#pragma once

#include <stdbool.h>

typedef struct {
    void *loader_task;
    void *decode_task;
    void *output_task;
    volatile bool run;
    int tasks_started;
    bool codec_open;
} audio_fw_runtime_t;

void audio_fw_runtime_reset(audio_fw_runtime_t *runtime);
void audio_fw_runtime_begin_load(audio_fw_runtime_t *runtime);
void audio_fw_runtime_mark_task_started(audio_fw_runtime_t *runtime);
void audio_fw_runtime_mark_stopped(audio_fw_runtime_t *runtime);
