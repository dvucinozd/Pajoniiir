#include "audio_keylock_wsola2.h"

#include <math.h>
#include <stddef.h>

/* Search work per stretched frame. A hop is AUDIO_KEYLOCK2_HOP frames; the
 * search needs CORR + CAND reads, (2 * SEEK / DEC + 1) coarse candidates and
 * 2 * FINE + 1 fine ones: 2048 / 6 + 257 / 2 + 7 = 478 frames, well inside
 * the 1536-frame hop at any tempo (the hop length is fixed in stretched
 * frames). */
#define KL2_READS_PER_FRAME 6u
#define KL2_COARSE_PER_FRAME 2u
#define KL2_FINE_RADIUS 3
#define KL2_TEMPO_UNITY_EPS 0.0001f

enum {
    KL2_STAGE_DONE = 0,
    KL2_STAGE_REF,
    KL2_STAGE_CAND,
    KL2_STAGE_COARSE,
    KL2_STAGE_FINE,
};

#define KL2_DEC_CORR (AUDIO_KEYLOCK2_CORR / AUDIO_KEYLOCK2_DECIMATION)
#define KL2_COARSE_COUNT ((AUDIO_KEYLOCK2_CAND_FRAMES - AUDIO_KEYLOCK2_CORR) / \
                          AUDIO_KEYLOCK2_DECIMATION + 1u)
#define KL2_SEARCH_FRAMES \
    ((AUDIO_KEYLOCK2_CORR + AUDIO_KEYLOCK2_CAND_FRAMES + KL2_READS_PER_FRAME - 1u) / \
         KL2_READS_PER_FRAME + \
     (KL2_COARSE_COUNT + KL2_COARSE_PER_FRAME - 1u) / KL2_COARSE_PER_FRAME + \
     2u * KL2_FINE_RADIUS + 1u)
/* The search must finish inside the hop, and the grain must cover its own
 * crossfade. C99: negative array size on failure. */
typedef char kl2_search_fits_in_hop[KL2_SEARCH_FRAMES < AUDIO_KEYLOCK2_HOP ? 1 : -1];
typedef char kl2_overlap_fits_in_hop[AUDIO_KEYLOCK2_OVERLAP <= AUDIO_KEYLOCK2_HOP ? 1 : -1];

static float clamp_factor(float v, float lo, float hi)
{
    if (!isfinite(v)) return 1.0f;
    return v < lo ? lo : (v > hi ? hi : v);
}

static int16_t round_clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static int16_t mono_of(audio_mixer_frame_t f)
{
    return (int16_t)(((int32_t)f.left + (int32_t)f.right) >> 1);
}

/* Prefers correlation over energy without a sqrt: num * |num| / energy has
 * the sign and ordering of the normalised cross-correlation squared. */
static float corr_score(float num, float energy)
{
    return num * fabsf(num) / (energy + 1.0f);
}

/* Transport position at the start of the next hop, if the tempo holds. */
static uint64_t predict_next_boundary(const audio_keylock2_t *s)
{
    float ahead = s->logical_fraction +
                  (float)(AUDIO_KEYLOCK2_HOP - s->hop_pos) * s->tempo_factor;
    /* ahead <= HOP * 2 + 1: a 32-bit conversion, no libgcc float->u64
     * helper (it would run from flash). */
    return s->logical_seq + (uint32_t)(ahead + 0.5f);
}

static void search_begin(audio_keylock2_t *s)
{
    s->nominal = predict_next_boundary(s);
    s->search_failed = false;
    s->candidates = 0u;
    const uint64_t natural = s->grain_in + AUDIO_KEYLOCK2_HOP;
    const uint64_t gap = natural > s->nominal ? natural - s->nominal
                                              : s->nominal - natural;
    /* Unity tempo: keep the natural continuation (bit-exact, no drift) as
     * long as it stays within the search radius of the transport. */
    if (s->tempo_factor > 1.0f - KL2_TEMPO_UNITY_EPS &&
        s->tempo_factor < 1.0f + KL2_TEMPO_UNITY_EPS &&
        gap <= AUDIO_KEYLOCK2_SEEK) {
        s->next_grain = natural;
        s->stage = KL2_STAGE_DONE;
        return;
    }
    uint64_t first = s->nominal > AUDIO_KEYLOCK2_SEEK
        ? s->nominal - AUDIO_KEYLOCK2_SEEK : 0u;
    if (first < s->floor_seq) first = s->floor_seq;
    s->cand_first = first;
    s->cand_count = AUDIO_KEYLOCK2_CAND_FRAMES;
    s->next_grain = s->nominal;          /* fallback until a candidate wins */
    s->best_score = -INFINITY;
    s->best_offset = (int32_t)(s->nominal - first);
    s->stage = KL2_STAGE_REF;
    s->stage_pos = 0u;
}

static void search_fail(audio_keylock2_t *s)
{
    s->search_failed = true;
    s->next_grain = s->nominal;
    s->stage = KL2_STAGE_DONE;
}

static bool read_mono(audio_keylock2_t *s, uint64_t seq, int16_t *out)
{
    audio_mixer_frame_t f;
    if (!s->read(s->read_ctx, seq, &f)) return false;
    *out = mono_of(f);
    return true;
}

static void search_step(audio_keylock2_t *s)
{
    switch (s->stage) {
    case KL2_STAGE_REF:
        for (uint32_t n = 0u; n < KL2_READS_PER_FRAME; n++) {
            const uint32_t i = s->stage_pos;
            if (!read_mono(s, s->grain_in + AUDIO_KEYLOCK2_HOP + i,
                           &s->ref_mono[i])) {
                search_fail(s);
                return;
            }
            if ((i % AUDIO_KEYLOCK2_DECIMATION) == AUDIO_KEYLOCK2_DECIMATION - 1u) {
                float sum = 0.0f;
                for (uint32_t k = 0u; k < AUDIO_KEYLOCK2_DECIMATION; k++) {
                    sum += (float)s->ref_mono[i - k];
                }
                s->ref_dec[i / AUDIO_KEYLOCK2_DECIMATION] = sum;
            }
            if (++s->stage_pos == AUDIO_KEYLOCK2_CORR) {
                s->stage = KL2_STAGE_CAND;
                s->stage_pos = 0u;
                return;
            }
        }
        return;
    case KL2_STAGE_CAND:
        for (uint32_t n = 0u; n < KL2_READS_PER_FRAME; n++) {
            const uint32_t i = s->stage_pos;
            if (!read_mono(s, s->cand_first + i, &s->cand_mono[i])) {
                search_fail(s);
                return;
            }
            if ((i % AUDIO_KEYLOCK2_DECIMATION) == AUDIO_KEYLOCK2_DECIMATION - 1u) {
                float sum = 0.0f;
                for (uint32_t k = 0u; k < AUDIO_KEYLOCK2_DECIMATION; k++) {
                    sum += (float)s->cand_mono[i - k];
                }
                s->cand_dec[i / AUDIO_KEYLOCK2_DECIMATION] = sum;
            }
            if (++s->stage_pos == s->cand_count) {
                float energy = 0.0f;
                for (uint32_t k = 0u; k < KL2_DEC_CORR; k++) {
                    energy += s->cand_dec[k] * s->cand_dec[k];
                }
                s->energy = energy;
                s->stage = KL2_STAGE_COARSE;
                s->stage_pos = 0u;
                return;
            }
        }
        return;
    case KL2_STAGE_COARSE:
        for (uint32_t n = 0u; n < KL2_COARSE_PER_FRAME; n++) {
            const uint32_t d = s->stage_pos;
            const float *c = &s->cand_dec[d];
            float num = 0.0f;
            for (uint32_t k = 0u; k < KL2_DEC_CORR; k++) {
                num += s->ref_dec[k] * c[k];
            }
            s->candidates++;
            const float score = corr_score(num, s->energy);
            if (score > s->best_score) {
                s->best_score = score;
                s->best_offset = (int32_t)(d * AUDIO_KEYLOCK2_DECIMATION);
            }
            if (++s->stage_pos == KL2_COARSE_COUNT) {
                /* Fine stage starts from the coarse winner; it re-scores it
                 * at full rate, so the best score restarts too. */
                s->stage = KL2_STAGE_FINE;
                s->stage_pos = 0u;
                s->best_score = -INFINITY;
                s->fine_centre = s->best_offset;
                return;
            }
            /* Slide the decimated energy window by one. */
            const float out_v = c[0];
            const float in_v = c[KL2_DEC_CORR];
            s->energy += in_v * in_v - out_v * out_v;
            if (s->energy < 0.0f) s->energy = 0.0f;
        }
        return;
    case KL2_STAGE_FINE: {
        const int32_t offset = s->fine_centre - KL2_FINE_RADIUS + (int32_t)s->stage_pos;
        const int32_t last = (int32_t)(s->cand_count - AUDIO_KEYLOCK2_CORR);
        if (offset >= 0 && offset <= last) {
            const int16_t *c = &s->cand_mono[offset];
            float num = 0.0f;
            float energy = 0.0f;
            for (uint32_t k = 0u; k < AUDIO_KEYLOCK2_CORR; k += 2u) {
                const float cv = (float)c[k];
                num += (float)s->ref_mono[k] * cv;
                energy += cv * cv;
            }
            s->candidates++;
            const float score = corr_score(num, energy);
            if (score > s->best_score) {
                s->best_score = score;
                s->best_offset = offset;
            }
        }
        if (++s->stage_pos > 2u * (uint32_t)KL2_FINE_RADIUS) {
            s->next_grain = s->best_score > 0.0f
                ? s->cand_first + (uint64_t)s->best_offset : s->nominal;
            s->stage = KL2_STAGE_DONE;
        }
        return;
    }
    default:
        return;
    }
}

/* One frame of the time-stretched stream at the source rate. */
static bool stretch_next(audio_keylock2_t *s, audio_mixer_frame_t *out)
{
    if (s->hop_pos == 0u) search_begin(s);
    if (s->stage != KL2_STAGE_DONE) search_step(s);

    const uint32_t j = s->hop_pos;
    audio_mixer_frame_t in_f;
    if (!s->read(s->read_ctx, s->grain_in + j, &in_f)) return false;
    if (s->first_hop || j >= AUDIO_KEYLOCK2_OVERLAP) {
        *out = in_f;
    } else {
        audio_mixer_frame_t out_f;
        if (!s->read(s->read_ctx, s->grain_out + AUDIO_KEYLOCK2_HOP + j, &out_f)) {
            return false;
        }
        const float x = ((float)j + 0.5f) * (1.0f / (float)AUDIO_KEYLOCK2_OVERLAP);
        const float w_in = x * x * (3.0f - 2.0f * x);
        const float w_out = 1.0f - w_in;
        out->left = round_clamp_i16(w_out * (float)out_f.left + w_in * (float)in_f.left);
        out->right = round_clamp_i16(w_out * (float)out_f.right + w_in * (float)in_f.right);
    }

    s->logical_fraction += s->tempo_factor;
    const uint32_t advance = (uint32_t)s->logical_fraction;
    s->logical_seq += advance;
    s->logical_fraction -= (float)advance;
    s->pulled_consumed += advance;

    if (++s->hop_pos == AUDIO_KEYLOCK2_HOP) {
        /* A search cannot outlast the hop (see KL2_READS_PER_FRAME); if it
         * ever did, its best candidate so far or the nominal grain is used. */
        if (s->stage != KL2_STAGE_DONE) {
            s->next_grain = s->best_score > 0.0f
                ? s->cand_first + (uint64_t)s->best_offset : s->nominal;
            s->stage = KL2_STAGE_DONE;
        }
        s->last_candidates = s->candidates;
        s->grain_out = s->grain_in;
        s->grain_in = s->next_grain;
        s->first_hop = false;
        s->hop_pos = 0u;
    }
    return true;
}

static bool pop_stretched(void *ctx, audio_mixer_frame_t *out)
{
    audio_keylock2_t *s = (audio_keylock2_t *)ctx;
    if (s->pull_failed) return false;
    if (!stretch_next(s, out)) {
        s->pull_failed = true;
        return false;
    }
    return true;
}

void audio_keylock2_reset(audio_keylock2_t *s, uint64_t start)
{
    if (!s) return;
    s->initialized = true;
    s->first_hop = true;
    s->tempo_factor = 1.0f;
    s->rate_ratio = 1.0f;
    s->logical_seq = start;
    s->logical_fraction = 0.0f;
    s->floor_seq = start;
    s->grain_out = start;
    s->grain_in = start;
    s->hop_pos = 0u;
    s->stage = KL2_STAGE_DONE;
    s->stage_pos = 0u;
    s->search_failed = false;
    s->nominal = start;
    s->next_grain = start;
    s->best_score = -INFINITY;
    s->best_offset = 0;
    s->candidates = 0u;
    s->last_candidates = 0u;
    wsola2_resampler_reset(&s->resampler);
    s->read = NULL;
    s->read_ctx = NULL;
    s->pulled_consumed = 0u;
    s->pull_failed = false;
}

void audio_keylock2_configure(audio_keylock2_t *s, float tempo, float ratio)
{
    if (!s) return;
    const float next_tempo = clamp_factor(tempo, 0.50f, 2.00f);
    const float next_ratio = clamp_factor(ratio, 0.25f, 4.00f);
    if (next_ratio != s->rate_ratio) {
        /* Entering or changing the rate conversion: start it clean. */
        wsola2_resampler_reset(&s->resampler);
    }
    s->tempo_factor = next_tempo;
    s->rate_ratio = next_ratio;
}

bool audio_keylock2_next(audio_keylock2_t *s, audio_keylock_read_fn read, void *ctx,
                         audio_mixer_frame_t *out, uint32_t *consumed,
                         uint64_t *play_seq)
{
    if (out) *out = (audio_mixer_frame_t){0};
    if (consumed) *consumed = 0u;
    if (!s || !s->initialized || !read || !out) return false;
    s->read = read;
    s->read_ctx = ctx;
    s->pulled_consumed = 0u;
    s->pull_failed = false;

    if (s->rate_ratio == 1.0f) {
        if (!stretch_next(s, out)) return false;
    } else {
        uint32_t stretched = 0u;
        *out = wsola2_resampler_next(&s->resampler, s->rate_ratio, pop_stretched,
                                    s, &stretched);
        if (s->pull_failed) return false;
    }
    if (consumed) *consumed = s->pulled_consumed;
    if (play_seq) *play_seq = s->logical_seq;
    return true;
}
