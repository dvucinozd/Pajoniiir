#include "audio_output_mixer.h"

static float clamp_gain(float gain)
{
    if (gain < 0.0f) return 0.0f;
    if (gain > 1.0f) return 1.0f;
    return gain;
}

static int32_t round_to_i32(float sample)
{
    return (int32_t)(sample >= 0.0f ? sample + 0.5f : sample - 0.5f);
}

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
                                            uint32_t *out_deck1_consumed,
                                            audio_mixer_limiter_stats_t *limiter_stats)
{
    uint32_t consumed0 = 0u;
    uint32_t consumed1 = 0u;
    audio_mixer_frame_t frame0 = next_deck_frame(deck0, &consumed0);
    audio_mixer_frame_t frame1 = next_deck_frame(deck1, &consumed1);

    if (out_deck0_consumed) *out_deck0_consumed = consumed0;
    if (out_deck1_consumed) *out_deck1_consumed = consumed1;

    float gain0 = deck0 ? clamp_gain(deck0->gain) : 0.0f;
    float gain1 = deck1 ? clamp_gain(deck1->gain) : 0.0f;
    int32_t left = round_to_i32(((float)frame0.left * gain0) +
                                ((float)frame1.left * gain1));
    int32_t right = round_to_i32(((float)frame0.right * gain0) +
                                 ((float)frame1.right * gain1));

    return (audio_mixer_frame_t) {
        .left = audio_mixer_limit_master_sample(left, limiter_stats),
        .right = audio_mixer_limit_master_sample(right, limiter_stats),
    };
}
