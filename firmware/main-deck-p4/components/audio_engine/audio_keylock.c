#include "audio_keylock.h"

static float clamp_factor(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int16_t lerp_i16(int16_t a, int16_t b, float f)
{
    float v = (float)a + ((float)b - (float)a) * f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static bool read_fractional(audio_keylock_read_fn read, void *ctx,
                            uint64_t origin_seq, float seq,
                            audio_mixer_frame_t *out)
{
    if (!read || !out || seq < 0.0f) return false;
    uint32_t whole = (uint32_t)seq;
    float fraction = seq - (float)whole;
    audio_mixer_frame_t a = {0}, b = {0};
    uint64_t absolute = origin_seq + whole;
    if (!read(ctx, absolute, &a)) return false;
    if (fraction <= 0.000001f || !read(ctx, absolute + 1u, &b)) {
        *out = a;
        return true;
    }
    out->left = lerp_i16(a.left, b.left, fraction);
    out->right = lerp_i16(a.right, b.right, fraction);
    return true;
}

static float select_grain_start(audio_keylock_t *s, audio_keylock_read_fn read,
                                void *ctx, float nominal)
{
    if (s->tempo_factor > 0.9999f && s->tempo_factor < 1.0001f) return nominal;
    float reference = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * s->rate_ratio;
    float best = nominal;
    uint64_t best_error = UINT64_MAX;
    int radius = (int)(48.0f * s->rate_ratio + 0.5f);
    if (radius < 12) radius = 12;
    for (int delta = -radius; delta <= radius; delta++) {
        float candidate = nominal + (float)delta;
        if (candidate < 0.0f) continue;
        uint64_t error = 0u;
        bool valid = true;
        for (uint32_t i = 0; i < 64u; i += 4u) {
            audio_mixer_frame_t a, b;
            float offset = i * s->rate_ratio;
            if (!read_fractional(read, ctx, s->origin_seq, reference + offset, &a) ||
                !read_fractional(read, ctx, s->origin_seq, candidate + offset, &b)) {
                valid = false;
                break;
            }
            int32_t dl = (int32_t)a.left - b.left;
            int32_t dr = (int32_t)a.right - b.right;
            error += (uint64_t)((int64_t)dl * dl + (int64_t)dr * dr);
        }
        if (valid && error < best_error) {
            best_error = error;
            best = candidate;
        }
    }
    return best;
}

static void rebase_coordinates(audio_keylock_t *s)
{
    /* Keep float coordinates small enough for stable sub-frame precision and
     * retain one grain of history before grain_a for overlap reads. */
    const float rebase_threshold = 16384.0f;
    if (!s || s->grain_a < rebase_threshold) return;
    uint32_t shift = (uint32_t)(s->grain_a - (float)AUDIO_KEYLOCK_SYNTH_HOP);
    s->origin_seq += shift;
    s->grain_a -= (float)shift;
    s->grain_b -= (float)shift;
}

void audio_keylock_reset(audio_keylock_t *s, uint64_t start)
{
    if (!s) return;
    *s = (audio_keylock_t){ .initialized = true, .initial_half = true,
        .origin_seq = start, .logical_seq = start,
        .grain_a = 0.0f, .logical_fraction = 0.0f,
        .tempo_factor = 1.0f, .rate_ratio = 1.0f };
    s->grain_b = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP;
}

void audio_keylock_configure(audio_keylock_t *s, float tempo, float ratio)
{
    if (!s) return;
    s->tempo_factor = clamp_factor(tempo, 0.50f, 2.00f);
    s->rate_ratio = clamp_factor(ratio, 0.25f, 4.00f);
}

bool audio_keylock_next(audio_keylock_t *s, audio_keylock_read_fn read, void *ctx,
                        audio_mixer_frame_t *out, uint32_t *consumed, uint64_t *play_seq)
{
    if (out) *out = (audio_mixer_frame_t){0};
    if (consumed) *consumed = 0u;
    if (!s || !s->initialized || !read || !out) return false;
    float ratio = s->rate_ratio;
    if (s->initial_half) {
        if (!read_fractional(read, ctx, s->origin_seq,
                             s->grain_a + s->phase * ratio, out)) return false;
    } else {
        audio_mixer_frame_t a = {0}, b = {0};
        float pa = s->grain_a + (AUDIO_KEYLOCK_SYNTH_HOP + s->phase) * ratio;
        float pb = s->grain_b + s->phase * ratio;
        if (!read_fractional(read, ctx, s->origin_seq, pa, &a) ||
            !read_fractional(read, ctx, s->origin_seq, pb, &b)) return false;
        float fade = (float)(s->phase + 1u) / AUDIO_KEYLOCK_SYNTH_HOP;
        out->left = lerp_i16(a.left, b.left, fade);
        out->right = lerp_i16(a.right, b.right, fade);
    }
    uint64_t before = s->logical_seq;
    s->logical_fraction += s->tempo_factor * ratio;
    uint32_t advance = (uint32_t)s->logical_fraction;
    s->logical_seq += advance;
    s->logical_fraction -= (float)advance;
    uint64_t after = s->logical_seq;
    if (consumed) *consumed = (uint32_t)(after - before);
    if (play_seq) *play_seq = after;
    if (++s->phase >= AUDIO_KEYLOCK_SYNTH_HOP) {
        s->phase = 0u;
        if (s->initial_half) {
            s->initial_half = false;
            float nominal = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * ratio *
                            s->tempo_factor;
            s->grain_b = select_grain_start(s, read, ctx, nominal);
        } else {
            s->grain_a = s->grain_b;
            float nominal = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * ratio *
                            s->tempo_factor;
            s->grain_b = select_grain_start(s, read, ctx, nominal);
        }
        rebase_coordinates(s);
    }
    return true;
}
