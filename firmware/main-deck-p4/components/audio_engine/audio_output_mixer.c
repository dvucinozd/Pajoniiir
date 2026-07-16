#include "audio_output_mixer.h"

static float clamp_gain(float gain)
{
    if (!(gain > 0.0f)) return 0.0f;
    if (gain > 2.0f) return 2.0f;
    return gain;
}

float audio_output_mixer_rate_ratio(uint32_t source_sample_rate,
                                    uint32_t output_sample_rate)
{
    if (source_sample_rate == 0u || output_sample_rate == 0u) {
        return 1.0f;
    }
    return (float)source_sample_rate / (float)output_sample_rate;
}

float audio_output_mixer_resample_factor(float pitch_factor,
                                         uint32_t source_sample_rate,
                                         uint32_t output_sample_rate)
{
    if (!(pitch_factor > 0.0f)) {
        pitch_factor = 1.0f;
    }
    return pitch_factor *
           audio_output_mixer_rate_ratio(source_sample_rate, output_sample_rate);
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
        float rate_ratio = deck->keylock_rate_ratio > 0.0f
            ? deck->keylock_rate_ratio
            : audio_output_mixer_rate_ratio(deck->source_sample_rate,
                                             deck->output_sample_rate);
        if (deck->keylock_render(deck->keylock_ctx, deck->pitch_factor,
                                 rate_ratio,
                                 &frame, out_consumed)) {
            return frame;
        }
        /* Near EOF there may be enough PCM for ordinary resampling but not for
         * the key-lock look-ahead window. Fall through and drain it normally. */
    }

    if (!deck->resampler) {
        return (audio_mixer_frame_t){ 0 };
    }

    float effective_pitch = deck->resample_factor > 0.0f
        ? deck->resample_factor
        : audio_output_mixer_resample_factor(deck->pitch_factor,
                                              deck->source_sample_rate,
                                              deck->output_sample_rate);

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

void audio_output_mixer_prepare_controls(audio_output_mixer_controls_t *out,
                                         bool deck0_pfl,
                                         bool deck1_pfl,
                                         audio_output_headphone_mode_t headphone_mode,
                                         uint16_t headphone_mix,
                                         uint16_t headphone_level,
                                         bool master_cue_enabled)
{
    if (!out) return;
    float master_gain = normalized_headphone_master_mix(headphone_mix);
    *out = (audio_output_mixer_controls_t) {
        .deck0_pfl = deck0_pfl,
        .deck1_pfl = deck1_pfl,
        .headphone_mode = headphone_mode,
        .headphone_master_gain = master_gain,
        .headphone_cue_gain = 1.0f - master_gain,
        .headphone_level_gain = normalized_headphone_level(headphone_level),
        .master_cue_enabled = master_cue_enabled,
    };
}

audio_output_mix_result_t audio_output_mixer_next_prepared(
                                                       const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       const audio_output_mixer_controls_t *controls,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats)
{
    const audio_output_mixer_controls_t defaults = {
        .headphone_mode = AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
        .headphone_master_gain = 1.0f,
        .headphone_level_gain = 1.0f,
        .master_cue_enabled = true,
    };
    if (!controls) controls = &defaults;
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

    float pfl_gain0 = controls->deck0_pfl ? 1.0f : 0.0f;
    float pfl_gain1 = controls->deck1_pfl ? 1.0f : 0.0f;
    audio_mixer_frame_t pfl = {
        .left = audio_mixer_mix_sample(frame0.left, frame1.left, pfl_gain0, pfl_gain1),
        .right = audio_mixer_mix_sample(frame0.right, frame1.right, pfl_gain0, pfl_gain1),
    };

    audio_mixer_frame_t monitor_master = controls->master_cue_enabled
        ? master : (audio_mixer_frame_t){ 0 };
    int16_t master_mono = controls->master_cue_enabled ? mono_from_frame(master) : 0;
    int16_t pfl_mono = mono_from_frame(pfl);
    audio_mixer_frame_t headphone = {
        .left = audio_mixer_mix_sample(monitor_master.left, pfl_mono,
                                       controls->headphone_master_gain,
                                       controls->headphone_cue_gain),
        .right = audio_mixer_mix_sample(monitor_master.right, pfl_mono,
                                        controls->headphone_master_gain,
                                        controls->headphone_cue_gain),
    };

    if (controls->headphone_mode == AUDIO_OUTPUT_HEADPHONE_CUE_MONO) {
        headphone.left = pfl_mono;
        headphone.right = pfl_mono;
    } else if (controls->headphone_mode == AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO) {
        headphone.left = master_mono;
        headphone.right = pfl_mono;
    }

    headphone.left = audio_mixer_mix_sample(headphone.left, 0,
                                             controls->headphone_level_gain, 0.0f);
    headphone.right = audio_mixer_mix_sample(headphone.right, 0,
                                              controls->headphone_level_gain, 0.0f);

    return (audio_output_mix_result_t) {
        .master = master,
        .headphone = headphone,
        .deck_frame = { frame0, frame1 },
    };
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
    audio_output_mixer_controls_t controls;
    audio_output_mixer_prepare_controls(&controls, deck0_pfl, deck1_pfl,
                                        headphone_mode, headphone_mix,
                                        headphone_level, master_cue_enabled);
    return audio_output_mixer_next_prepared(deck0, deck1, &controls,
                                             out_deck0_consumed,
                                             out_deck1_consumed,
                                             limiter_stats);
}
