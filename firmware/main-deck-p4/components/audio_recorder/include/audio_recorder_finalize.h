#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*audio_recorder_finalize_step_fn)(void *ctx);

typedef enum {
    AUDIO_RECORDER_FINALIZE_STAGE_NONE = 0,
    AUDIO_RECORDER_FINALIZE_STAGE_PATCH,
    AUDIO_RECORDER_FINALIZE_STAGE_SYNC,
    AUDIO_RECORDER_FINALIZE_STAGE_CLOSE,
    AUDIO_RECORDER_FINALIZE_STAGE_PUBLISH,
} audio_recorder_finalize_stage_t;

typedef struct {
    audio_recorder_finalize_stage_t failed_stage;
    bool closed;
    bool published;
} audio_recorder_finalize_result_t;

/* Patch, sync and close are always attempted in that order. Publication is
 * attempted only when every durability step succeeded and allow_publish is
 * true. This makes `.part` the durable failure state. */
audio_recorder_finalize_result_t audio_recorder_finalize_run(
    void *ctx,
    audio_recorder_finalize_step_fn patch,
    audio_recorder_finalize_step_fn sync,
    audio_recorder_finalize_step_fn close,
    audio_recorder_finalize_step_fn publish,
    bool allow_publish);

#ifdef __cplusplus
}
#endif
