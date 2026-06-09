#include "audio_output_mixer.h"

static audio_mixer_frame_t next_deck_frame(const audio_output_mixer_deck_t *deck,
                                           uint32_t *out_consumed)
{
    if (out_consumed) *out_consumed = 0u;
    if (!deck || !deck->active || !deck->resampler) {
        return (audio_mixer_frame_t){ 0 };
    }

    return audio_resampler_next(deck->resampler,
                                deck->pitch_factor,
                                deck->pop_source,
                                deck->source_ctx,
                                out_consumed);
}

audio_mixer_frame_t audio_output_mixer_next(const audio_output_mixer_deck_t *deck0,
                                            const audio_output_mixer_deck_t *deck1,
                                            uint32_t *out_deck0_consumed,
                                            uint32_t *out_deck1_consumed)
{
    uint32_t consumed0 = 0u;
    uint32_t consumed1 = 0u;
    audio_mixer_frame_t frame0 = next_deck_frame(deck0, &consumed0);
    audio_mixer_frame_t frame1 = next_deck_frame(deck1, &consumed1);

    if (out_deck0_consumed) *out_deck0_consumed = consumed0;
    if (out_deck1_consumed) *out_deck1_consumed = consumed1;

    return (audio_mixer_frame_t) {
        .left = audio_mixer_mix_sample(frame0.left,
                                       frame1.left,
                                       deck0 ? deck0->gain : 0.0f,
                                       deck1 ? deck1->gain : 0.0f),
        .right = audio_mixer_mix_sample(frame0.right,
                                        frame1.right,
                                        deck0 ? deck0->gain : 0.0f,
                                        deck1 ? deck1->gain : 0.0f),
    };
}
