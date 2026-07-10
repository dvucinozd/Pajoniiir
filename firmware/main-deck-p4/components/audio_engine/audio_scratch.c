#include "audio_scratch.h"

void audio_scratch_init(audio_scratch_t *s)
{
    if (!s) return;
    s->head_back = 0.0f;
    s->velocity = 0.0f;
    s->velocity_per_tick = AUDIO_SCRATCH_DEFAULT_VELOCITY_PER_TICK;
    s->velocity_decay = AUDIO_SCRATCH_DEFAULT_VELOCITY_DECAY;
    s->velocity_max = AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX;
    s->active = false;
}

void audio_scratch_config(audio_scratch_t *s, float velocity_per_tick,
                          float velocity_decay, float velocity_max)
{
    if (!s) return;
    s->velocity_per_tick = velocity_per_tick;
    s->velocity_decay = velocity_decay;
    s->velocity_max = velocity_max;
}

void audio_scratch_seed(audio_scratch_t *s, float head_back)
{
    if (!s) return;
    s->head_back = head_back < 0.0f ? 0.0f : head_back;
    s->velocity = 0.0f;
    s->active = true;
}

void audio_scratch_end(audio_scratch_t *s)
{
    if (!s) return;
    s->active = false;
    s->velocity = 0.0f;
}

void audio_scratch_jog(audio_scratch_t *s, int16_t ticks)
{
    if (!s || ticks == 0) return;
    float v = s->velocity + (float)ticks * s->velocity_per_tick;
    if (v > s->velocity_max) v = s->velocity_max;
    if (v < -s->velocity_max) v = -s->velocity_max;
    s->velocity = v;
}

float audio_scratch_head_back(const audio_scratch_t *s)
{
    return s ? s->head_back : 0.0f;
}

bool audio_scratch_is_active(const audio_scratch_t *s)
{
    return s && s->active;
}

bool audio_scratch_render(audio_scratch_t *s, const audio_scratch_buffer_t *buf,
                          int16_t *out_left, int16_t *out_right)
{
    if (out_left) *out_left = 0;
    if (out_right) *out_right = 0;
    if (!s || !s->active || !buf || !out_left || !out_right) {
        return false;
    }

    uint32_t filled = audio_scratch_buffer_used(buf);
    if (filled < 2u) {
        s->velocity *= s->velocity_decay;  /* nothing to read yet */
        return false;
    }
    float max_back = (float)(filled - 1u);

    /* Stopped platter -> silence (a still record makes no sound), head held. */
    if (s->velocity > -AUDIO_SCRATCH_SILENCE_VELOCITY &&
        s->velocity < AUDIO_SCRATCH_SILENCE_VELOCITY) {
        s->velocity = 0.0f;
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
        s->head_back -= s->velocity;
        if (s->head_back < 0.0f) s->head_back = 0.0f;
        if (s->head_back > max_back) s->head_back = max_back;
        s->velocity *= s->velocity_decay;
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

    /* Advance (forward velocity moves toward newer -> smaller head_back), then
     * decay the velocity toward a stop. */
    s->head_back -= s->velocity;
    if (s->head_back < 0.0f) s->head_back = 0.0f;
    if (s->head_back > max_back) s->head_back = max_back;
    s->velocity *= s->velocity_decay;
    return true;
}
