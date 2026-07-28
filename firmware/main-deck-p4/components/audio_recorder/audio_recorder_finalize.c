#include "audio_recorder_finalize.h"

static void note_failure(audio_recorder_finalize_result_t *result,
                         audio_recorder_finalize_stage_t stage,
                         bool ok)
{
    if (!ok && result->failed_stage == AUDIO_RECORDER_FINALIZE_STAGE_NONE) {
        result->failed_stage = stage;
    }
}

audio_recorder_finalize_result_t audio_recorder_finalize_run(
    void *ctx,
    audio_recorder_finalize_step_fn patch,
    audio_recorder_finalize_step_fn sync,
    audio_recorder_finalize_step_fn close,
    audio_recorder_finalize_step_fn publish,
    bool allow_publish)
{
    audio_recorder_finalize_result_t result = {
        .failed_stage = AUDIO_RECORDER_FINALIZE_STAGE_NONE,
        .closed = false,
        .published = false,
    };
    if (!patch || !sync || !close || (allow_publish && !publish)) {
        result.failed_stage = AUDIO_RECORDER_FINALIZE_STAGE_PATCH;
        return result;
    }

    note_failure(&result, AUDIO_RECORDER_FINALIZE_STAGE_PATCH, patch(ctx));
    note_failure(&result, AUDIO_RECORDER_FINALIZE_STAGE_SYNC, sync(ctx));
    bool close_ok = close(ctx);
    result.closed = true;
    note_failure(&result, AUDIO_RECORDER_FINALIZE_STAGE_CLOSE, close_ok);

    if (allow_publish && result.failed_stage == AUDIO_RECORDER_FINALIZE_STAGE_NONE) {
        bool publish_ok = publish(ctx);
        result.published = publish_ok;
        note_failure(&result, AUDIO_RECORDER_FINALIZE_STAGE_PUBLISH, publish_ok);
    }
    return result;
}
