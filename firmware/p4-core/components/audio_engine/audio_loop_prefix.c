#include "audio_loop_prefix.h"
#include <stddef.h>

bool audio_loop_prefix_capture(audio_loop_prefix_t *p,
                               const audio_pcm_timeline_t *t,
                               uint64_t seq, uint32_t start_ms,
                               uint32_t rate, uint32_t session)
{
    if (!p) return false;
    p->valid = false;
    if (!t || !p->pcm || !p->capacity || !rate || !session) return false;
    uint64_t oldest = audio_pcm_timeline_oldest_seq(t);
    uint64_t end = audio_pcm_timeline_write_seq(t);
    if (seq < oldest || seq >= end) return false;
    uint64_t available = end - seq;
    uint32_t frames = rate < p->capacity ? rate : p->capacity;
    if (available < frames) frames = (uint32_t)available;
    /* Align the prefix to the same integer-ms seek interface. */
    uint32_t ms = (uint32_t)((uint64_t)frames * 1000u / rate);
    frames = (uint32_t)((uint64_t)ms * rate / 1000u);
    if (ms < 100u) return false;
    for (uint32_t i = 0; i < frames; ++i) {
        audio_mixer_frame_t f;
        if (!audio_pcm_timeline_read(t, seq + i, &f)) return false;
        p->pcm[i * 2u] = f.left;
        p->pcm[i * 2u + 1u] = f.right;
    }
    p->frames = frames;
    p->sample_rate = rate;
    p->start_ms = start_ms;
    p->session = session;
    p->valid = true;
    return true;
}

audio_loop_prefix_plan_t audio_loop_prefix_plan(const audio_loop_prefix_t *p,
                                               uint32_t session,
                                               uint32_t start_ms,
                                               uint32_t end_ms)
{
    audio_loop_prefix_plan_t plan = {0};
    if (!p || !p->valid || !p->pcm || !p->sample_rate || !p->frames ||
        p->session != session || p->start_ms != start_ms || end_ms <= start_ms)
        return plan;
    uint64_t period = (uint64_t)(end_ms - start_ms) * p->sample_rate / 1000u;
    if (!period) return plan;
    if (period <= p->frames) {
        /* Whole short loop is cached: publish complete laps, then resume at IN.
         * This supplies runway even when one lap is shorter than seek latency. */
        plan.period_frames = (uint32_t)period;
        plan.frames = p->frames / plan.period_frames * plan.period_frames;
        plan.seek_ms = start_ms;
    } else {
        uint32_t ms = (uint32_t)((uint64_t)p->frames * 1000u / p->sample_rate);
        /* Capture floors ms->frames; round-trip flooring can lose one ms.
         * Use the first ms whose conversion reproduces the captured frame count. */
        if ((uint64_t)ms * p->sample_rate / 1000u != p->frames) ++ms;
        plan.frames = (uint32_t)((uint64_t)ms * p->sample_rate / 1000u);
        if (plan.frames > p->frames) return (audio_loop_prefix_plan_t){0};
        plan.period_frames = plan.frames;
        plan.seek_ms = start_ms + ms;
    }
    return plan;
}

bool audio_loop_prefix_frame(const audio_loop_prefix_t *p,
                             const audio_loop_prefix_plan_t *plan,
                             uint32_t index, audio_mixer_frame_t *out)
{
    if (!p || !p->valid || !p->pcm || !plan || !out ||
        index >= plan->frames || !plan->period_frames ||
        plan->period_frames > p->frames) return false;
    uint32_t at = index % plan->period_frames;
    *out = (audio_mixer_frame_t){p->pcm[at * 2u], p->pcm[at * 2u + 1u]};
    return true;
}
