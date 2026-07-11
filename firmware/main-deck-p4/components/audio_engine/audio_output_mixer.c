#include "audio_output_mixer.h"

static float clamp_gain(float gain)
{
    if (gain < 0.0f) return 0.0f;
    if (gain > 2.0f) return 2.0f;
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
    if (!deck || !deck->active) {
        return (audio_mixer_frame_t){ 0 };
    }

    /* Scratch source (vinyl mode): jog-driven read over the capture buffer,
     * already at output rate, so it bypasses the resampler and consumes nothing
     * from the ring. The EQ/FX chain below still applies to the returned frame. */
    if (deck->scratch_active && deck->scratch_render) {
        audio_mixer_frame_t frame = { 0 };
        deck->scratch_render(deck->scratch_ctx, &frame);
        return frame;
    }

    if (deck->keylock_active && deck->keylock_render) {
        audio_mixer_frame_t frame = {0};
        if (deck->keylock_render(deck->keylock_ctx, deck->pitch_factor,
                                 &frame, out_consumed)) {
            return frame;
        }
        /* Near EOF there may be enough PCM for ordinary resampling but not for
         * the key-lock look-ahead window. Fall through and drain it normally. */
    }

    if (!deck->resampler) {
        return (audio_mixer_frame_t){ 0 };
    }

    float effective_pitch = deck->pitch_factor;
    if (deck->source_sample_rate > 0u && deck->output_sample_rate > 0u) {
        effective_pitch *= (float)deck->source_sample_rate / (float)deck->output_sample_rate;
    }

    return audio_resampler_next(deck->resampler,
                                effective_pitch,
                                deck->pop_source,
                                deck->source_ctx,
                                out_consumed);
}

static audio_mixer_frame_t apply_deck_eq(const audio_output_mixer_deck_t *deck,
                                         audio_mixer_frame_t frame)
{
    if (!deck || !deck->eq) {
        return frame;
    }
    return audio_eq_process_frame(deck->eq, frame);
}

static audio_mixer_frame_t apply_deck_filter(const audio_output_mixer_deck_t *deck,
                                             audio_mixer_frame_t frame)
{
    if (!deck || !deck->filter) {
        return frame;
    }
    return audio_filter_process_frame(deck->filter, deck->filter_enabled, frame);
}

static audio_mixer_frame_t apply_deck_beat_fx_filter(const audio_output_mixer_deck_t *deck,
                                                     audio_mixer_frame_t frame)
{
    if (!deck || !deck->beat_fx_filter) {
        return frame;
    }
    return audio_filter_process_frame(deck->beat_fx_filter, deck->beat_fx_filter_enabled, frame);
}

static audio_mixer_frame_t apply_deck_beat_fx_flanger(const audio_output_mixer_deck_t *deck,
                                                      audio_mixer_frame_t frame)
{
    if (!deck || !deck->beat_fx_flanger || !deck->beat_fx_flanger_enabled) {
        return frame;
    }
    return audio_flanger_fx_process_frame(deck->beat_fx_flanger, frame);
}

static audio_mixer_frame_t apply_deck_beat_fx_echo(const audio_output_mixer_deck_t *deck,
                                                   audio_mixer_frame_t frame)
{
    if (!deck || !deck->beat_fx_echo) {
        return frame;
    }
    /* Keep processing after switch-off while the echo tail rings out. */
    if (!deck->beat_fx_echo_enabled && !audio_delay_fx_is_ringing(deck->beat_fx_echo)) {
        return frame;
    }
    return audio_delay_fx_process_frame(deck->beat_fx_echo, frame);
}

static audio_mixer_frame_t apply_deck_pad_fx(const audio_output_mixer_deck_t *deck,
                                             audio_mixer_frame_t frame)
{
    if (!deck || !deck->pad_fx) {
        return frame;
    }
    return audio_pad_fx_process_frame(deck->pad_fx, frame);
}

audio_mixer_frame_t audio_output_mixer_next(const audio_output_mixer_deck_t *deck0,
                                            const audio_output_mixer_deck_t *deck1,
                                            uint32_t *out_deck0_consumed,
                                            uint32_t *out_deck1_consumed,
                                            audio_mixer_limiter_stats_t *limiter_stats)
{
    uint32_t consumed0 = 0u;
    uint32_t consumed1 = 0u;
    audio_mixer_frame_t frame0 = apply_deck_beat_fx_echo(deck0,
        apply_deck_beat_fx_flanger(deck0,
            apply_deck_beat_fx_filter(deck0,
                apply_deck_pad_fx(deck0,
                    apply_deck_filter(deck0, apply_deck_eq(deck0, next_deck_frame(deck0, &consumed0)))))));
    audio_mixer_frame_t frame1 = apply_deck_beat_fx_echo(deck1,
        apply_deck_beat_fx_flanger(deck1,
            apply_deck_beat_fx_filter(deck1,
                apply_deck_pad_fx(deck1,
                    apply_deck_filter(deck1, apply_deck_eq(deck1, next_deck_frame(deck1, &consumed1)))))));

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

static int16_t mono_from_frame(audio_mixer_frame_t frame)
{
    return audio_mixer_mix_sample(frame.left, frame.right, 0.5f, 0.5f);
}

static float normalized_headphone_master_mix(uint16_t raw)
{
    if (raw >= AUDIO_MIXER_CONTROL_MAX) {
        return 1.0f;
    }
    return (float)raw / (float)AUDIO_MIXER_CONTROL_MAX;
}

static float normalized_headphone_level(uint16_t raw)
{
    if (raw >= AUDIO_MIXER_CONTROL_MAX) {
        return 1.0f;
    }
    return (float)raw / (float)AUDIO_MIXER_CONTROL_MAX;
}

audio_output_mix_result_t audio_output_mixer_next_full_with_headphone_level(
                                                       const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       bool deck0_pfl,
                                                       bool deck1_pfl,
                                                       audio_output_headphone_mode_t headphone_mode,
                                                       uint16_t headphone_mix,
                                                       uint16_t headphone_level,
                                                       bool master_cue_enabled,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats)
{
    uint32_t consumed0 = 0u;
    uint32_t consumed1 = 0u;
    audio_mixer_frame_t frame0 = apply_deck_beat_fx_echo(deck0,
        apply_deck_beat_fx_flanger(deck0,
            apply_deck_beat_fx_filter(deck0,
                apply_deck_pad_fx(deck0,
                    apply_deck_filter(deck0, apply_deck_eq(deck0, next_deck_frame(deck0, &consumed0)))))));
    audio_mixer_frame_t frame1 = apply_deck_beat_fx_echo(deck1,
        apply_deck_beat_fx_flanger(deck1,
            apply_deck_beat_fx_filter(deck1,
                apply_deck_pad_fx(deck1,
                    apply_deck_filter(deck1, apply_deck_eq(deck1, next_deck_frame(deck1, &consumed1)))))));

    if (out_deck0_consumed) *out_deck0_consumed = consumed0;
    if (out_deck1_consumed) *out_deck1_consumed = consumed1;

    float gain0 = deck0 ? clamp_gain(deck0->gain) : 0.0f;
    float gain1 = deck1 ? clamp_gain(deck1->gain) : 0.0f;
    int32_t master_left = round_to_i32(((float)frame0.left * gain0) +
                                       ((float)frame1.left * gain1));
    int32_t master_right = round_to_i32(((float)frame0.right * gain0) +
                                        ((float)frame1.right * gain1));

    audio_mixer_frame_t master = {
        .left = audio_mixer_limit_master_sample(master_left, limiter_stats),
        .right = audio_mixer_limit_master_sample(master_right, limiter_stats),
    };

    float pfl_gain0 = deck0_pfl ? 1.0f : 0.0f;
    float pfl_gain1 = deck1_pfl ? 1.0f : 0.0f;
    audio_mixer_frame_t pfl = {
        .left = audio_mixer_mix_sample(frame0.left, frame1.left, pfl_gain0, pfl_gain1),
        .right = audio_mixer_mix_sample(frame0.right, frame1.right, pfl_gain0, pfl_gain1),
    };

    audio_mixer_frame_t monitor_master = master_cue_enabled ? master : (audio_mixer_frame_t){ 0 };
    int16_t master_mono = master_cue_enabled ? mono_from_frame(master) : 0;
    int16_t pfl_mono = mono_from_frame(pfl);
    float master_mix = normalized_headphone_master_mix(headphone_mix);
    float cue_mix = 1.0f - master_mix;
    audio_mixer_frame_t headphone = {
        .left = audio_mixer_mix_sample(monitor_master.left, pfl_mono, master_mix, cue_mix),
        .right = audio_mixer_mix_sample(monitor_master.right, pfl_mono, master_mix, cue_mix),
    };

    if (headphone_mode == AUDIO_OUTPUT_HEADPHONE_CUE_MONO) {
        headphone.left = pfl_mono;
        headphone.right = pfl_mono;
    } else if (headphone_mode == AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO) {
        headphone.left = master_mono;
        headphone.right = pfl_mono;
    }

    float headphone_gain = normalized_headphone_level(headphone_level);
    headphone.left = audio_mixer_mix_sample(headphone.left, 0, headphone_gain, 0.0f);
    headphone.right = audio_mixer_mix_sample(headphone.right, 0, headphone_gain, 0.0f);

    return (audio_output_mix_result_t) {
        .master = master,
        .headphone = headphone,
        .deck_frame = { frame0, frame1 },
    };
}

audio_output_mix_result_t audio_output_mixer_next_full(const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       bool deck0_pfl,
                                                       bool deck1_pfl,
                                                       audio_output_headphone_mode_t headphone_mode,
                                                       uint16_t headphone_mix,
                                                       bool master_cue_enabled,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats)
{
    return audio_output_mixer_next_full_with_headphone_level(deck0,
                                                            deck1,
                                                            deck0_pfl,
                                                            deck1_pfl,
                                                            headphone_mode,
                                                            headphone_mix,
                                                            AUDIO_MIXER_CONTROL_MAX,
                                                            master_cue_enabled,
                                                            out_deck0_consumed,
                                                            out_deck1_consumed,
                                                            limiter_stats);
}
