#include "wsola2_resampler.h"
#include "wsola2_resampler_sinc_table.h"

#include <math.h>
#include <string.h>

#define WSOLA2_RESAMPLER_MIN_FACTOR 0.01f
#define WSOLA2_RESAMPLER_MAX_FACTOR 16.0f

static float sanitize_pitch_factor(float factor)
{
    if (!isfinite(factor)) return 1.0f;
    if (factor < WSOLA2_RESAMPLER_MIN_FACTOR) return WSOLA2_RESAMPLER_MIN_FACTOR;
    if (factor > WSOLA2_RESAMPLER_MAX_FACTOR) return WSOLA2_RESAMPLER_MAX_FACTOR;
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

void wsola2_resampler_reset(wsola2_resampler_state_t *state)
{
    if (!state) return;
    memset(state->history, 0, sizeof(state->history));
    state->write = 0u;
    state->phase_q32 = 0u;
    state->pitch_factor_bits = UINT32_MAX;
    state->step_q32 = 0u;
}

static void push_frame(wsola2_resampler_state_t *state, audio_mixer_frame_t frame)
{
    const uint32_t w = state->write;
    state->history[w] = frame;
    state->history[w + WSOLA2_RESAMPLER_TAPS] = frame;
    state->write = w + 1u < WSOLA2_RESAMPLER_TAPS ? w + 1u : 0u;
}

static int16_t round_clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

/* Interpolation point between window[TAPS/2 - 1] ("previous") and
 * window[TAPS/2] at the Q32 phase: the two phase rows around it are blended
 * linearly, then 16 taps per channel. */
static audio_mixer_frame_t interpolate(const wsola2_resampler_state_t *state)
{
    const audio_mixer_frame_t *window = &state->history[state->write];
    const uint32_t phase = state->phase_q32;
    const uint32_t row_shift = 27u;   /* 32 phases = top 5 bits */
    const uint32_t row = phase >> row_shift;
    const float blend = (float)(phase & ((1u << row_shift) - 1u)) *
                        (1.0f / (float)(1u << row_shift));
    const float *a = s_resampler_sinc[row];
    const float *b = s_resampler_sinc[row + 1u];
    float left = 0.0f;
    float right = 0.0f;
    for (uint32_t k = 0u; k < WSOLA2_RESAMPLER_TAPS; k++) {
        const float c = a[k] + blend * (b[k] - a[k]);
        left += c * (float)window[k].left;
        right += c * (float)window[k].right;
    }
    return (audio_mixer_frame_t) {
        .left = round_clamp_i16(left),
        .right = round_clamp_i16(right),
    };
}

audio_mixer_frame_t wsola2_resampler_next(wsola2_resampler_state_t *state,
                                         float pitch_factor,
                                         wsola2_resampler_pop_fn pop_source,
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
        /* Ring underrun: repeat the newest frame instead of a 0, so the
         * output settles on a flat hold rather than a click to silence.
         * Nothing is consumed, so pacing is unaffected. */
        audio_mixer_frame_t next = state->history[
            (state->write + 2u * WSOLA2_RESAMPLER_TAPS - 1u) % (2u * WSOLA2_RESAMPLER_TAPS)];
        if (pop_source && pop_source(source_ctx, &next)) {
            if (out_consumed) (*out_consumed)++;
        }
        push_frame(state, next);
    }

    /* Unity rate on a whole sample (48 kHz track, pitch 0): the source frame
     * itself, bit-exact, as with linear interpolation. Same alignment as the
     * filter at phase 0, so leaving this path does not shift the audio. */
    if (state->phase_q32 == 0u && state->step_q32 == (UINT64_C(1) << 32)) {
        return state->history[state->write + WSOLA2_RESAMPLER_TAPS / 2u - 1u];
    }
    return interpolate(state);
}
