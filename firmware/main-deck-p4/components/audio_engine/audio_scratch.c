#include "audio_scratch.h"

#include <limits.h>
#include <math.h>

#define AUDIO_SCRATCH_FRAMES_PER_TICK_MAX 100000.0f

void audio_scratch_init(audio_scratch_t *s)
{
    if (!s) return;
    s->head_back = 0.0f;
    s->velocity = 0.0f;
    s->velocity_target = 0.0f;
    __atomic_store_n(&s->pending_ticks, 0, __ATOMIC_RELAXED);
    s->window_pos = 0u;
    s->empty_windows = 0u;
    s->frames_per_tick = AUDIO_SCRATCH_DEFAULT_FRAMES_PER_TICK;
    s->rate_window_samples = AUDIO_SCRATCH_DEFAULT_RATE_WINDOW;
    s->slew_coef = AUDIO_SCRATCH_DEFAULT_SLEW_COEF;
    s->velocity_max = AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX;
    s->hold_windows = AUDIO_SCRATCH_DEFAULT_HOLD_WINDOWS;
    s->edge_latch = 0;
    s->edge_hits = 0u;
    s->active = false;
}

void audio_scratch_config(audio_scratch_t *s, float frames_per_tick,
                          uint32_t rate_window_samples, float slew_coef,
                          float velocity_max, uint32_t hold_windows)
{
    if (!s) return;
    if (!isfinite(frames_per_tick) || frames_per_tick <= 0.0f) {
        frames_per_tick = AUDIO_SCRATCH_DEFAULT_FRAMES_PER_TICK;
    } else if (frames_per_tick > AUDIO_SCRATCH_FRAMES_PER_TICK_MAX) {
        frames_per_tick = AUDIO_SCRATCH_FRAMES_PER_TICK_MAX;
    }
    if (!isfinite(slew_coef)) {
        slew_coef = AUDIO_SCRATCH_DEFAULT_SLEW_COEF;
    } else if (slew_coef < 0.0f) {
        slew_coef = 0.0f;
    } else if (slew_coef > 1.0f) {
        slew_coef = 1.0f;
    }
    if (!isfinite(velocity_max) || velocity_max <= 0.0f) {
        velocity_max = AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX;
    }
    s->frames_per_tick = frames_per_tick;
    s->rate_window_samples = rate_window_samples > 0u ? rate_window_samples : 1u;
    s->slew_coef = slew_coef;
    s->velocity_max = velocity_max;
    s->hold_windows = hold_windows;
}

void audio_scratch_seed(audio_scratch_t *s, float head_back)
{
    if (!s) return;
    if (!isfinite(head_back)) head_back = 0.0f;
    s->head_back = head_back < 0.0f ? 0.0f : head_back;
    s->velocity = 0.0f;
    s->velocity_target = 0.0f;
    __atomic_store_n(&s->pending_ticks, 0, __ATOMIC_RELAXED);
    s->window_pos = 0u;
    s->empty_windows = 0u;
    s->edge_latch = 0;
    s->active = true;
}

void audio_scratch_end(audio_scratch_t *s)
{
    if (!s) return;
    s->active = false;
    s->velocity = 0.0f;
    s->velocity_target = 0.0f;
    __atomic_store_n(&s->pending_ticks, 0, __ATOMIC_RELAXED);
    s->edge_latch = 0;
}

void audio_scratch_jog(audio_scratch_t *s, int16_t ticks)
{
    if (!s || ticks == 0) return;
    /* Bank the motion; the render loop turns the banked ticks into a rate over a
     * fixed window. Saturating CAS avoids signed overflow if the output task is
     * stalled while a pathological stream keeps delivering jog events. */
    int32_t current = __atomic_load_n(&s->pending_ticks, __ATOMIC_RELAXED);
    for (;;) {
        int64_t sum = (int64_t)current + (int32_t)ticks;
        int32_t next = sum > INT32_MAX ? INT32_MAX
                       : sum < INT32_MIN ? INT32_MIN
                                           : (int32_t)sum;
        if (__atomic_compare_exchange_n(&s->pending_ticks, &current, next,
                                        true, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
            break;
        }
    }
}

float audio_scratch_head_back(const audio_scratch_t *s)
{
    return s ? s->head_back : 0.0f;
}

bool audio_scratch_is_active(const audio_scratch_t *s)
{
    return s && s->active;
}

/* Advance the fixed-window rate estimate by one rendered sample, then slew the
 * playback velocity toward the current target. Every rate_window_samples the
 * banked ticks become a fresh target = ticks x frames_per_tick / window; between
 * windows the target holds, and a run of empty windows stops the platter. */
static void update_velocity(audio_scratch_t *s)
{
    if (++s->window_pos >= s->rate_window_samples) {
        s->window_pos = 0u;
        int32_t ticks = __atomic_exchange_n(&s->pending_ticks, 0, __ATOMIC_RELAXED);
        if (ticks != 0) {
            s->empty_windows = 0u;
            float v = ((float)ticks * s->frames_per_tick) /
                      (float)s->rate_window_samples;
            if (v > s->velocity_max) v = s->velocity_max;
            if (v < -s->velocity_max) v = -s->velocity_max;
            s->velocity_target = v;
        } else if (++s->empty_windows >= s->hold_windows) {
            s->velocity_target = 0.0f;   /* hand has stopped */
        }
        /* else: brief tickless gap -> HOLD the previous target (steady motion). */
    }

    s->velocity += (s->velocity_target - s->velocity) * s->slew_coef;
}

bool audio_scratch_render(audio_scratch_t *s, const audio_scratch_buffer_t *buf,
                          int16_t *out_left, int16_t *out_right)
{
    if (out_left) *out_left = 0;
    if (out_right) *out_right = 0;
    if (!s || !s->active || !buf || !out_left || !out_right) {
        return false;
    }

    update_velocity(s);

    /* Once a fast throw reaches a retained-window edge, hold a clean silent
     * stop instead of repeatedly accelerating into the clamp. Only an inward
     * velocity estimate releases the latch, matching a record stopped by a
     * physical end-stop and eliminating edge chatter from same-direction ticks. */
    if (s->edge_latch > 0) {
        if (s->velocity < -AUDIO_SCRATCH_SILENCE_VELOCITY ||
            s->velocity_target < -AUDIO_SCRATCH_SILENCE_VELOCITY) {
            s->edge_latch = 0;
        } else {
            s->velocity = 0.0f;
            return false;
        }
    } else if (s->edge_latch < 0) {
        if (s->velocity > AUDIO_SCRATCH_SILENCE_VELOCITY ||
            s->velocity_target > AUDIO_SCRATCH_SILENCE_VELOCITY) {
            s->edge_latch = 0;
        } else {
            s->velocity = 0.0f;
            return false;
        }
    }

    uint32_t filled = audio_scratch_buffer_used(buf);
    if (filled < 2u) {
        return false;  /* nothing to read yet */
    }
    float max_back = (float)(filled - 1u);

    /* Stopped platter -> silence (a still record makes no sound), head held. */
    if (s->velocity > -AUDIO_SCRATCH_SILENCE_VELOCITY &&
        s->velocity < AUDIO_SCRATCH_SILENCE_VELOCITY) {
        return false;
    }

    /* Keep the head inside the window (float safety). */
    if (s->head_back < 0.0f) s->head_back = 0.0f;
    if (s->head_back > max_back) s->head_back = max_back;

    /* Running past a window edge (forward past the newest frame, or reverse past
     * the oldest) -> silence, not a held tone. Keep integrating so a reversal
     * walks the head back into the window. */
    bool past_new_edge = (s->head_back <= 0.0f && s->velocity > 0.0f);
    bool past_old_edge = (s->head_back >= max_back && s->velocity < 0.0f);
    if (past_new_edge || past_old_edge) {
        s->edge_latch = past_new_edge ? 1 : -1;
        s->edge_hits++;
        s->velocity = 0.0f;
        return false;
    }

    /* Linear-interpolate between the two frames bracketing the head. `k0` is the
     * newer of the pair (smaller frames-back), `k0+1` the older; frac walks from
     * newer (0) to older (1). */
    uint32_t k0 = (uint32_t)s->head_back;
    float frac = s->head_back - (float)k0;
    if (k0 + 1u > filled - 1u) {
        k0 = filled - 2u;
        frac = 1.0f;
    }

    int16_t l0 = 0, r0 = 0, l1 = 0, r1 = 0;
    audio_scratch_buffer_read_frame_back(buf, k0, &l0, &r0);
    audio_scratch_buffer_read_frame_back(buf, k0 + 1u, &l1, &r1);
    *out_left = (int16_t)((1.0f - frac) * (float)l0 + frac * (float)l1);
    *out_right = (int16_t)((1.0f - frac) * (float)r0 + frac * (float)r1);

    /* Advance (forward velocity moves toward newer -> smaller head_back). */
    s->head_back -= s->velocity;
    if (s->head_back < 0.0f) s->head_back = 0.0f;
    if (s->head_back > max_back) s->head_back = max_back;
    return true;
}

uint32_t audio_scratch_track_position_ms(uint32_t newest_pos_ms,
                                         float head_back_frames,
                                         uint32_t sample_rate,
                                         bool loop_active,
                                         uint32_t loop_start_ms,
                                         uint32_t loop_end_ms)
{
    if (sample_rate == 0u) return newest_pos_ms;
    uint32_t back_ms = 0u;
    if (isfinite(head_back_frames) && head_back_frames > 0.0f) {
        double calculated = ((double)head_back_frames * 1000.0) /
                            (double)sample_rate;
        back_ms = calculated >= (double)UINT32_MAX ? UINT32_MAX
                                                    : (uint32_t)calculated;
    }

    if (loop_active && loop_end_ms > loop_start_ms &&
        newest_pos_ms >= loop_start_ms && newest_pos_ms <= loop_end_ms) {
        uint32_t loop_ms = loop_end_ms - loop_start_ms;
        uint32_t newest_offset = (newest_pos_ms - loop_start_ms) % loop_ms;
        uint32_t back_offset = back_ms % loop_ms;
        uint32_t target_offset =
            (newest_offset + loop_ms - back_offset) % loop_ms;
        return loop_start_ms + target_offset;
    }

    return newest_pos_ms > back_ms ? newest_pos_ms - back_ms : 0u;
}
