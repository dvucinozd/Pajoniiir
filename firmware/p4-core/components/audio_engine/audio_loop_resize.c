/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#include "audio_loop_resize.h"

static uint64_t ms_frames(uint32_t ms, uint32_t sample_rate)
{
    return ((uint64_t)ms * sample_rate) / 1000u;
}

audio_loop_resize_plan_t audio_loop_resize_plan(const audio_loop_resize_in_t *in)
{
    audio_loop_resize_plan_t plan = {0};
    if (!in || in->sample_rate == 0u || in->old_end_ms <= in->old_start_ms) return plan;
    const uint64_t fs = in->frames_since_seek;
    const uint64_t used = in->ring_frames;
    uint64_t keep = 0u;                /* frames ahead of the playhead that stay */
    if (in->since_wrap && fs < used) {
        /* Ahead of the playhead: [.. old end], whole passes of the old loop
         * (a short loop wraps many times in the ring), then [old start ..
         * cursor). Only the first old end counts. */
        if (in->new_start_ms == in->old_start_ms && in->new_end_ms == in->old_end_ms) return plan;
        const uint64_t pass = ms_frames(in->old_end_ms - in->old_start_ms, in->sample_rate);
        if (pass == 0u) return plan;
        const uint64_t first = (used - fs - 1u) % pass + 1u;
        if (in->new_end_ms > in->old_end_ms) {
            keep = first;
            plan.seek_ms = in->old_end_ms;
            plan.exact = true;
        } else {
            const uint64_t back = ms_frames(in->old_end_ms - in->new_end_ms, in->sample_rate);
            if (back >= first) return plan;                  /* new end played: jump */
            keep = first - back;
            plan.seek_ms = in->new_start_ms;
        }
    } else {
        /* One piece, ending at the cursor: only a new end before it cuts. */
        if (in->new_end_ms <= in->seek_base_ms) return plan;
        const uint64_t to_end = ms_frames(in->new_end_ms - in->seek_base_ms, in->sample_rate);
        if (to_end >= fs) return plan;
        if (fs - to_end >= used) return plan;                /* new end played: jump */
        keep = used - (fs - to_end);
        plan.seek_ms = in->new_start_ms;
    }
    plan.cut = true;
    plan.drop_frames = (uint32_t)(used - keep);
    return plan;
}

bool audio_loop_resize_jump_ms(uint32_t position_ms, uint32_t start_ms, uint32_t end_ms,
                               uint32_t *target_ms)
{
    if (end_ms <= start_ms || position_ms < end_ms || !target_ms) return false;
    *target_ms = start_ms + (position_ms - start_ms) % (end_ms - start_ms);
    return true;
}
