#include "audio_resampler.h"

#include <math.h>
#include <string.h>

#define AUDIO_RESAMPLER_MIN_FACTOR 0.01f
#define AUDIO_RESAMPLER_MAX_FACTOR 16.0f

static float sanitize_pitch_factor(float factor)
{
    if (!isfinite(factor)) return 1.0f;
    if (factor < AUDIO_RESAMPLER_MIN_FACTOR) return AUDIO_RESAMPLER_MIN_FACTOR;
    if (factor > AUDIO_RESAMPLER_MAX_FACTOR) return AUDIO_RESAMPLER_MAX_FACTOR;
    return factor;
}

static uint32_t float_bits(float value)
{
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* Convert a positive, finite binary32 pitch to Q32 using its IEEE-754 fields.
 * The sanitized exponent range is -7..4, so the shift is always 2..13. This
 * avoids both double arithmetic and a float-to-uint64 runtime helper. */
static uint64_t pitch_step_q32(uint32_t bits)
{
    uint32_t mantissa = (1u << 23) | (bits & 0x7FFFFFu);
    int32_t exponent = (int32_t)((bits >> 23) & 0xFFu) - 127;
    return (uint64_t)mantissa << (uint32_t)(exponent + 9);
}

static float phase_fraction_float(uint32_t phase_q32)
{
    /* Build 1.f directly, then subtract one. Retaining the upper 23 fraction
     * bits is exactly the precision a binary32 interpolator can consume. */
    uint32_t bits = 0x3F800000u | (phase_q32 >> 9);
    float one_to_two = 1.0f;
    memcpy(&one_to_two, &bits, sizeof(one_to_two));
    return one_to_two - 1.0f;
}

void audio_resampler_reset(audio_resampler_state_t *state)
{
    if (!state) return;
    state->previous = (audio_mixer_frame_t){ 0 };
    state->current = (audio_mixer_frame_t){ 0 };
    state->phase_q32 = 0u;
    state->pitch_factor_bits = UINT32_MAX;
    state->step_q32 = 0u;
}

audio_mixer_frame_t audio_resampler_next(audio_resampler_state_t *state,
                                         float pitch_factor,
                                         audio_resampler_pop_fn pop_source,
                                         void *source_ctx,
                                         uint32_t *out_consumed)
{
    if (out_consumed) *out_consumed = 0u;
    if (!state) return (audio_mixer_frame_t){ 0 };

    float factor = sanitize_pitch_factor(pitch_factor);
    uint32_t factor_bits = float_bits(factor);
    if (factor_bits != state->pitch_factor_bits) {
        state->pitch_factor_bits = factor_bits;
        state->step_q32 = pitch_step_q32(factor_bits);
    }

    uint64_t phase = (uint64_t)state->phase_q32 + state->step_q32;
    uint32_t source_frames = (uint32_t)(phase >> 32);
    state->phase_q32 = (uint32_t)phase;
    while (source_frames-- > 0u) {
        state->previous = state->current;

        audio_mixer_frame_t next = { 0 };
        if (pop_source && pop_source(source_ctx, &next)) {
            state->current = next;
            if (out_consumed) (*out_consumed)++;
        }
        /* Ring underrun: leave `current` unchanged instead of snapping it to 0.
         * `previous` was just set to `current` above, so both now hold the last
         * delivered frame and the interpolation is a flat hold rather than a
         * click to silence. Nothing is consumed, so pacing is unaffected. */
    }

    float t = phase_fraction_float(state->phase_q32);
    float inv = 1.0f - t;
    return (audio_mixer_frame_t) {
        .left = (int16_t)(inv * (float)state->previous.left +
                          t * (float)state->current.left),
        .right = (int16_t)(inv * (float)state->previous.right +
                           t * (float)state->current.right),
    };
}
