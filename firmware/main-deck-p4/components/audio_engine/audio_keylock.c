#include "audio_keylock.h"

#include <math.h>
#include <string.h>

typedef struct {
    audio_keylock_read_fn read;
    void *ctx;
    uint64_t first;
    uint32_t count;
    audio_mixer_frame_t *frames;
    uint8_t *valid;
} keylock_read_cache_t;

enum {
    KEYLOCK_REFERENCE_STRIDE = 32,
    KEYLOCK_REFERENCE_COUNT = 2,
    KEYLOCK_COARSE_POINTS = 8,
    KEYLOCK_REFINE_RADIUS = 3,
};

static bool read_cached(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    keylock_read_cache_t *cache = ctx;
    if (seq < cache->first || seq - cache->first >= cache->count) {
        return cache->read(cache->ctx, seq, out);
    }
    uint32_t index = (uint32_t)(seq - cache->first);
    if (cache->valid[index] == 0u) {
        cache->valid[index] = cache->read(cache->ctx, seq,
                                          &cache->frames[index]) ? 1u : 2u;
    }
    if (cache->valid[index] != 1u) return false;
    *out = cache->frames[index];
    return true;
}

static float clamp_factor(float v, float lo, float hi)
{
    if (!isfinite(v)) return 1.0f;
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

static uint32_t absolute_i32(int32_t value)
{
    return (uint32_t)(value < 0 ? -value : value);
}

static bool candidate_sad(audio_keylock_t *s,
                          keylock_read_cache_t *cache,
                          const audio_mixer_frame_t *reference_frames,
                          uint32_t reference_count,
                          float candidate,
                          uint32_t stop_at,
                          uint32_t *out_error)
{
    uint32_t error = 0u;
    s->last_search_candidates++;
    for (uint32_t sample = 0u; sample < reference_count; sample++) {
        audio_mixer_frame_t b;
        float offset = (float)(sample * KEYLOCK_REFERENCE_STRIDE) *
                       s->rate_ratio;
        if (!read_fractional(read_cached, cache, s->origin_seq,
                             candidate + offset, &b)) {
            return false;
        }
        int32_t dl = (int32_t)reference_frames[sample].left - b.left;
        int32_t dr = (int32_t)reference_frames[sample].right - b.right;
        error += absolute_i32(dl) + absolute_i32(dr);
        if (error >= stop_at) break;
    }
    *out_error = error;
    return true;
}

static float select_grain_start(audio_keylock_t *s, audio_keylock_read_fn read,
                                void *ctx, float nominal)
{
    if (s->tempo_factor > 0.9999f && s->tempo_factor < 1.0001f) return nominal;
    float reference = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP * s->rate_ratio;
    float best = nominal;
    uint32_t best_error = UINT32_MAX;
    int radius = (int)(48.0f * s->rate_ratio + 0.5f);
    if (radius < 12) radius = 12;

    /* The reference window is identical for every candidate.  Reading and
     * interpolating it inside the candidate loop doubled canonical-timeline
     * traffic in the most expensive Master Tempo hot path. */
    audio_mixer_frame_t reference_frames[KEYLOCK_REFERENCE_COUNT];
    uint32_t reference_count = 0u;
    for (uint32_t i = 0; i < 64u; i += KEYLOCK_REFERENCE_STRIDE) {
        float offset = (float)i * s->rate_ratio;
        if (!read_fractional(read, ctx, s->origin_seq, reference + offset,
                             &reference_frames[reference_count])) {
            return nominal;
        }
        reference_count++;
    }

    float first = nominal - (float)radius;
    if (first < 0.0f) first = 0.0f;
    uint32_t first_frame = (uint32_t)first;
    uint32_t end_frame = (uint32_t)(nominal + (float)radius +
                                    60.0f * s->rate_ratio) + 2u;
    uint32_t count = end_frame - first_frame;
    if (count > AUDIO_KEYLOCK_SEARCH_CACHE_FRAMES) {
        count = AUDIO_KEYLOCK_SEARCH_CACHE_FRAMES;
    }
    memset(s->search_valid, 0, count);
    keylock_read_cache_t cache = {
        .read = read,
        .ctx = ctx,
        .first = s->origin_seq + first_frame,
        .count = count,
        .frames = s->search_frames,
        .valid = s->search_valid,
    };

    /* Two MT decks share a 5.8-ms output deadline. Sample the complete search
     * radius at eight evenly spaced points, then inspect seven frames around
     * the best point. This caps every hop at 15 candidates; the previous
     * multi-stage search reached 30 and starved the high-rate decoder. */
    s->last_search_candidates = 0u;
    int center = 0;
    for (int point = 0; point < KEYLOCK_COARSE_POINTS; point++) {
        int delta = -radius +
                    (2 * radius * point) / (KEYLOCK_COARSE_POINTS - 1);
        float candidate = nominal + (float)delta;
        if (candidate < 0.0f) continue;
        uint32_t error = 0u;
        if (candidate_sad(s, &cache, reference_frames, reference_count,
                          candidate, best_error, &error) &&
            error < best_error) {
            best_error = error;
            best = candidate;
            center = delta;
        }
    }
    int first_delta = center - KEYLOCK_REFINE_RADIUS;
    int last_delta = center + KEYLOCK_REFINE_RADIUS;
    if (first_delta < -radius) first_delta = -radius;
    if (last_delta > radius) last_delta = radius;
    for (int delta = first_delta; delta <= last_delta; delta++) {
        float candidate = nominal + (float)delta;
        if (candidate < 0.0f) continue;
        uint32_t error = 0u;
        if (candidate_sad(s, &cache, reference_frames, reference_count,
                          candidate, best_error, &error) &&
            error < best_error) {
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
    memset(s, 0, sizeof(*s));
    s->initialized = s->initial_half = true;
    s->origin_seq = s->logical_seq = start;
    s->tempo_factor = s->rate_ratio = 1.0f;
    s->grain_b = s->grain_a + AUDIO_KEYLOCK_SYNTH_HOP;
}

void audio_keylock_configure(audio_keylock_t *s, float tempo, float ratio)
{
    if (!s) return;
    float next_tempo = clamp_factor(tempo, 0.50f, 2.00f);
    float next_ratio = clamp_factor(ratio, 0.25f, 4.00f);
    if (s->tempo_factor == next_tempo && s->rate_ratio == next_ratio) return;
    s->tempo_factor = next_tempo;
    s->rate_ratio = next_ratio;
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
        /* Search correction chooses grain alignment; it must not become the
         * transport clock. Derive the next nominal position from the integrated
         * logical source cursor so small tempo changes cannot be cancelled by
         * accumulated correlation offsets. */
        float nominal = (float)(s->logical_seq - s->origin_seq) +
                        s->logical_fraction;
        if (s->initial_half) {
            s->initial_half = false;
        } else {
            s->grain_a = s->grain_b;
        }
        s->grain_b = select_grain_start(s, read, ctx, nominal);
        rebase_coordinates(s);
    }
    return true;
}
