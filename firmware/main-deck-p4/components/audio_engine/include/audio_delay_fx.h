#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_mixer.h"

typedef struct {
    bool enabled;
    uint32_t delay_ms;
    uint16_t wet_q15;
    uint16_t feedback_q15;
} audio_delay_fx_config_t;

typedef struct {
    int16_t *left;
    int16_t *right;
    uint32_t capacity_frames;
    uint32_t sample_rate;
    uint32_t write_index;
    uint32_t delay_frames;
    audio_delay_fx_config_t config;
    bool allocated;
} audio_delay_fx_t;

void audio_delay_fx_init(audio_delay_fx_t *fx,
                         int16_t *left,
                         int16_t *right,
                         uint32_t capacity_frames,
                         uint32_t sample_rate);
void audio_delay_fx_reset(audio_delay_fx_t *fx);
void audio_delay_fx_configure(audio_delay_fx_t *fx, const audio_delay_fx_config_t *config);
audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in);
bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx);
uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx);
