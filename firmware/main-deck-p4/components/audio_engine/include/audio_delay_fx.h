#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_mixer.h"

typedef enum {
    /* Keep ECHO at zero so existing zero-initialized configurations retain
     * their historical feedback-delay behaviour. */
    AUDIO_DELAY_FX_MODE_ECHO = 0,
    AUDIO_DELAY_FX_MODE_DELAY,
} audio_delay_fx_mode_t;

typedef struct {
    bool enabled;
    audio_delay_fx_mode_t mode;
    uint32_t delay_ms;
    uint16_t wet_q15;
    uint16_t feedback_q15;
} audio_delay_fx_config_t;

typedef struct {
    float *left;
    float *right;
    uint32_t capacity_frames;
    uint32_t sample_rate;
    uint32_t write_index;
    uint32_t delay_frames;
    audio_delay_fx_config_t config;
    bool allocated;
    /* One-pole low-pass state in the feedback path (repeats get darker). */
    float fb_lp[2];
    uint16_t damp_alpha_q15;
    /* Wet/feedback gains ramp toward the configured targets to de-click
     * knob moves while the echo is running. */
    uint16_t wet_cur_q15;
    uint16_t feedback_cur_q15;
    /* Frames of tail left to ring out after the effect is switched off. */
    uint32_t tail_frames_remaining;
} audio_delay_fx_t;

void audio_delay_fx_init(audio_delay_fx_t *fx,
                         float *left,
                         float *right,
                         uint32_t capacity_frames,
                         uint32_t sample_rate);
void audio_delay_fx_reset(audio_delay_fx_t *fx);
void audio_delay_fx_configure(audio_delay_fx_t *fx, const audio_delay_fx_config_t *config);
audio_dsp_frame_t audio_delay_fx_process_dsp_frame(audio_delay_fx_t *fx,
                                                   audio_dsp_frame_t in);
audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in);
bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx);
bool audio_delay_fx_is_ringing(const audio_delay_fx_t *fx);
uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx);
