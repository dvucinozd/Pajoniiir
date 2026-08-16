#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_delay_fx.h"
#include "audio_eq.h"
#include "audio_filter.h"
#include "audio_flanger_fx.h"
#include "audio_mixer.h"
#include "audio_pad_fx.h"
#include "audio_resampler.h"

/* Optional scratch source (vinyl mode Phase 4): when `scratch_active`, the deck
 * frame comes from `scratch_render` (a jog-driven read over the scratch buffer,
 * already at output rate) instead of the resampler+ring, and the normal EQ/FX
 * chain still applies. Fills *out and returns true if audio was produced (false
 * -> silence). Consumes nothing from the ring. */
typedef bool (*audio_output_scratch_fn)(void *ctx, audio_mixer_frame_t *out);
typedef bool (*audio_output_keylock_fn)(void *ctx, float tempo_factor,
                                        float rate_ratio,
                                        audio_mixer_frame_t *out,
                                        uint32_t *out_consumed);

typedef struct {
    bool active;
    float pitch_factor;
    /* Optional block-precomputed values. Zero retains the compatibility
     * fallback for standalone callers. */
    float resample_factor;
    float keylock_rate_ratio;
    uint32_t source_sample_rate;
    uint32_t output_sample_rate;
    /* Channel TRIM, applied once to the wide source before EQ/FX. A zero value
     * retains unity for compatibility with zero-initialized callers. */
    float pre_gain;
    /* Post-DSP channel fader/crossfader/master-volume gain. */
    float gain;
    audio_eq_state_t *eq;
    audio_filter_state_t *filter;
    bool filter_enabled;
    audio_filter_state_t *beat_fx_filter;
    bool beat_fx_filter_enabled;
    audio_flanger_fx_t *beat_fx_flanger;
    bool beat_fx_flanger_enabled;
    audio_delay_fx_t *beat_fx_echo;
    bool beat_fx_echo_enabled;
    audio_pad_fx_state_t *pad_fx;
    audio_resampler_state_t *resampler;
    audio_resampler_pop_fn pop_source;
    void *source_ctx;
    bool keylock_active;
    audio_output_keylock_fn keylock_render;
    void *keylock_ctx;
    bool scratch_active;
    audio_output_scratch_fn scratch_render;
    void *scratch_ctx;
} audio_output_mixer_deck_t;

typedef enum {
    AUDIO_OUTPUT_HEADPHONE_MASTER_MONO = 0,
    AUDIO_OUTPUT_HEADPHONE_CUE_MONO,
    AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO,
} audio_output_headphone_mode_t;

typedef struct {
    audio_mixer_frame_t master;
    audio_mixer_frame_t headphone;
    audio_mixer_frame_t deck_frame[2];
    audio_dsp_frame_t deck_dsp[2];
} audio_output_mix_result_t;

typedef struct {
    bool deck0_pfl;
    bool deck1_pfl;
    audio_output_headphone_mode_t headphone_mode;
    float headphone_master_gain;
    float headphone_cue_gain;
    float headphone_level_gain;
    float master_trim_gain;
    bool master_cue_enabled;
} audio_output_mixer_controls_t;

float audio_output_mixer_rate_ratio(uint32_t source_sample_rate,
                                    uint32_t output_sample_rate);
float audio_output_mixer_resample_factor(float pitch_factor,
                                         uint32_t source_sample_rate,
                                         uint32_t output_sample_rate);
void audio_output_mixer_prepare_controls(audio_output_mixer_controls_t *out,
                                         bool deck0_pfl,
                                         bool deck1_pfl,
                                         audio_output_headphone_mode_t headphone_mode,
                                         uint16_t headphone_mix,
                                         uint16_t headphone_level,
                                         float master_trim_gain,
                                         bool master_cue_enabled);

audio_output_mix_result_t audio_output_mixer_next_prepared(
                                                       const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       const audio_output_mixer_controls_t *controls,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats);

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
                                                       audio_mixer_limiter_stats_t *limiter_stats);
