/* Adapted from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#include "audio_cue_preroll.h"

uint32_t audio_cue_preroll_arm(audio_cue_preroll_t *p, bool arm,
                               uint32_t target_ms, uint32_t sample_rate,
                               uint32_t max_pre_frames)
{
    if (!p) return target_ms;
    p->frames = 0u;
    p->pending = false;
    if (!arm || !sample_rate || !target_ms) return target_ms;
    uint64_t max_ms = (uint64_t)max_pre_frames * 1000u / sample_rate;
    uint32_t pre_ms = max_ms < target_ms ? (uint32_t)max_ms : target_ms;
    p->frames = (uint32_t)((uint64_t)pre_ms * sample_rate / 1000u);
    p->pending = p->frames > 0u;
    return target_ms - pre_ms;
}

bool audio_cue_preroll_publish_point(const audio_cue_preroll_t *p,
                                     uint64_t write_seq, bool eof,
                                     uint64_t *playhead)
{
    if (!p || !p->pending || !playhead) return false;
    if (write_seq >= p->frames) {
        *playhead = p->frames;
        return true;
    }
    if (eof) {
        *playhead = write_seq;
        return true;
    }
    return false;
}
