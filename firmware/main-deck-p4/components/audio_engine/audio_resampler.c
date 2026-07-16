#include "audio_resampler.h"

#include <math.h>

#define AUDIO_RESAMPLER_MIN_FACTOR 0.01f
#define AUDIO_RESAMPLER_MAX_FACTOR 16.0f

static float sanitize_pitch_factor(float factor)
{
    if (!isfinite(factor)) return 1.0f;
    if (factor < AUDIO_RESAMPLER_MIN_FACTOR) return AUDIO_RESAMPLER_MIN_FACTOR;
    if (factor > AUDIO_RESAMPLER_MAX_FACTOR) return AUDIO_RESAMPLER_MAX_FACTOR;
    return factor;
}

void audio_resampler_reset(audio_resampler_state_t *state)
{
    if (!state) return;
    state->previous = (audio_mixer_frame_t){ 0 };
    state->current = (audio_mixer_frame_t){ 0 };
    state->fraction = 0.0;
}

audio_mixer_frame_t audio_resampler_next(audio_resampler_state_t *state,
                                         float pitch_factor,
                                         audio_resampler_pop_fn pop_source,
                                         void *source_ctx,
                                         uint32_t *out_consumed)
{
    if (out_consumed) *out_consumed = 0u;
    if (!state) return (audio_mixer_frame_t){ 0 };

    state->fraction += (double)sanitize_pitch_factor(pitch_factor);
    while (state->fraction >= 1.0) {
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

        state->fraction -= 1.0;
    }

    float t = (float)state->fraction;
    float inv = 1.0f - t;
    return (audio_mixer_frame_t) {
        .left = (int16_t)(inv * (float)state->previous.left +
                          t * (float)state->current.left),
        .right = (int16_t)(inv * (float)state->previous.right +
                           t * (float)state->current.right),
    };
}
