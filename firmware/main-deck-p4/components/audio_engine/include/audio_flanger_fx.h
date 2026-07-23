#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_mixer.h"

typedef struct {
    bool enabled;
    /* Full LFO sweep cycle (one down-and-up pass through the delay range). */
    uint32_t period_ms;
    /* Effect depth: scales both the wet mix and the feedback resonance. */
    uint16_t depth_q15;
} audio_flanger_fx_config_t;

typedef struct {
    int16_t *left;
    int16_t *right;
    uint32_t capacity_frames;
    uint32_t sample_rate;
    uint32_t write_index;
    audio_flanger_fx_config_t config;
    bool allocated;
    /* Triangle LFO as a wrapping phase accumulator. */
    uint32_t lfo_phase_q32;
    uint32_t lfo_step_q32;
    /* Modulated delay bounds in fractional frames (16.16). */
    uint32_t min_delay_q16;
    uint32_t span_delay_q16;
    /* Ramped gains, de-clicking depth-knob moves while running. */
    uint16_t wet_cur_q15;
    uint16_t feedback_cur_q15;
    /* Output normalisation, 1/(1+wet) in Q15, cached against the wet value it
     * was derived from so the divide happens only when wet actually moves. */
    uint16_t norm_q15;
    uint16_t norm_for_wet_q15;
    /* Bumped by audio_flanger_fx_tune(); forces the cached norm to refresh
     * when the tuning changes but the wet gain happens not to. */
    uint32_t norm_generation;
} audio_flanger_fx_t;

void audio_flanger_fx_init(audio_flanger_fx_t *fx,
                           int16_t *left,
                           int16_t *right,
                           uint32_t capacity_frames,
                           uint32_t sample_rate);
void audio_flanger_fx_reset(audio_flanger_fx_t *fx);
void audio_flanger_fx_configure(audio_flanger_fx_t *fx,
                                const audio_flanger_fx_config_t *config);
audio_mixer_frame_t audio_flanger_fx_process_frame(audio_flanger_fx_t *fx,
                                                   audio_mixer_frame_t in);
bool audio_flanger_fx_is_allocated(const audio_flanger_fx_t *fx);

/* Frames a caller must allocate per channel to cover the maximum modulated
 * delay (plus interpolation guard) at the given sample rate. */
uint32_t audio_flanger_fx_required_frames(uint32_t sample_rate);

/* Live tuning of the ceilings, for converging the sound by ear without a
 * reflash per attempt. min_delay_us takes effect on the next configure(), which
 * deck_core issues whenever the depth knob moves or Beat FX is toggled. */
void audio_flanger_fx_tune(uint16_t wet_max_q15, uint16_t fb_max_q15,
                           uint32_t min_delay_us, bool normalize);
void audio_flanger_fx_tuning(uint16_t *wet_max_q15, uint16_t *fb_max_q15,
                             uint32_t *min_delay_us, bool *normalize);
