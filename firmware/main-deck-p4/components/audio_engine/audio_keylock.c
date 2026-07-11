#include "audio_keylock.h"

static float clamp_factor(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int16_t lerp_i16(int16_t a, int16_t b, double f)
{
    double v = (double)a + ((double)b - (double)a) * f;
    if (v > 32767.0) v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    return (int16_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

static bool read_fractional(audio_keylock_read_fn read, void *ctx, double seq,
                            audio_mixer_frame_t *out)
{
    if (!read || !out || seq < 0.0) return false;
    uint64_t whole = (uint64_t)seq;
    double fraction = seq - (double)whole;
    audio_mixer_frame_t a = {0}, b = {0};
    if (!read(ctx, whole, &a)) return false;
    if (fraction <= 0.000001 || !read(ctx, whole + 1u, &b)) {
        *out = a;
        return true;
    }
    out->left = lerp_i16(a.left, b.left, fraction);
    out->right = lerp_i16(a.right, b.right, fraction);
    return true;
}

static double select_grain_start(audio_keylock_t *s, audio_keylock_read_fn read,
                                 void *ctx, double nominal)
{
    if (s->tempo_factor > 0.9999f && s->tempo_factor < 1.0001f) return nominal;
    double reference = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * s->rate_ratio;
    double best = nominal;
    uint64_t best_error = UINT64_MAX;
    int radius = (int)(48.0 * s->rate_ratio + 0.5);
    if (radius < 12) radius = 12;
    for (int delta = -radius; delta <= radius; delta++) {
        double candidate = nominal + delta;
        if (candidate < 0.0) continue;
        uint64_t error = 0u;
        bool valid = true;
        for (uint32_t i = 0; i < 64u; i += 4u) {
            audio_mixer_frame_t a, b;
            double offset = i * s->rate_ratio;
            if (!read_fractional(read, ctx, reference + offset, &a) ||
                !read_fractional(read, ctx, candidate + offset, &b)) {
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

void audio_keylock_reset(audio_keylock_t *s, uint64_t start)
{
    if (!s) return;
    *s = (audio_keylock_t){ .initialized = true, .initial_half = true,
        .grain_a = (double)start, .logical_seq = (double)start,
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
    double ratio = s->rate_ratio;
    if (s->initial_half) {
        if (!read_fractional(read, ctx, s->grain_a + s->phase * ratio, out)) return false;
    } else {
        audio_mixer_frame_t a = {0}, b = {0};
        double pa = s->grain_a + (AUDIO_KEYLOCK_SYNTH_HOP + s->phase) * ratio;
        double pb = s->grain_b + s->phase * ratio;
        if (!read_fractional(read, ctx, pa, &a) || !read_fractional(read, ctx, pb, &b)) return false;
        double fade = (double)(s->phase + 1u) / AUDIO_KEYLOCK_SYNTH_HOP;
        out->left = lerp_i16(a.left, b.left, fade);
        out->right = lerp_i16(a.right, b.right, fade);
    }
    uint64_t before = (uint64_t)s->logical_seq;
    s->logical_seq += s->tempo_factor * ratio;
    uint64_t after = (uint64_t)s->logical_seq;
    if (consumed) *consumed = (uint32_t)(after - before);
    if (play_seq) *play_seq = after;
    if (++s->phase >= AUDIO_KEYLOCK_SYNTH_HOP) {
        s->phase = 0u;
        if (s->initial_half) {
            s->initial_half = false;
            double nominal = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * ratio *
                             s->tempo_factor;
            s->grain_b = select_grain_start(s, read, ctx, nominal);
        } else {
            s->grain_a = s->grain_b;
            double nominal = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * ratio *
                             s->tempo_factor;
            s->grain_b = select_grain_start(s, read, ctx, nominal);
        }
    }
    return true;
}
