#include "audio_resampler.h"

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

    state->fraction += (double)pitch_factor;
    while (state->fraction >= 1.0) {
        state->previous = state->current;

        audio_mixer_frame_t next = { 0 };
        if (pop_source && pop_source(source_ctx, &next)) {
            state->current = next;
            if (out_consumed) (*out_consumed)++;
        } else {
            state->current = (audio_mixer_frame_t){ 0 };
        }

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
