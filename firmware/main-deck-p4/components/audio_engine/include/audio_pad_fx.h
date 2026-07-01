#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_delay_fx.h"
#include "audio_filter.h"
#include "audio_mixer.h"

typedef enum {
    AUDIO_PAD_FX_MODE_PAD_FX1 = 0,
    AUDIO_PAD_FX_MODE_PAD_FX2 = 1,
} audio_pad_fx_mode_t;

typedef enum {
    AUDIO_PAD_FX_KIND_NONE = 0,
    AUDIO_PAD_FX_KIND_FILTER = 1,
    AUDIO_PAD_FX_KIND_ECHO = 2,
} audio_pad_fx_kind_t;

typedef struct {
    audio_pad_fx_mode_t mode;
    uint8_t pad;
    bool active;
} audio_pad_fx_config_t;

typedef struct {
    audio_filter_state_t filter;
    audio_delay_fx_t echo;
    audio_pad_fx_config_t config;
    audio_pad_fx_kind_t kind;
    uint32_t echo_tail_frames_remaining;
    bool active;
    bool echo_tail_active;
} audio_pad_fx_state_t;

void audio_pad_fx_init(audio_pad_fx_state_t *fx, uint32_t sample_rate_hz);
void audio_pad_fx_init_with_echo_buffer(audio_pad_fx_state_t *fx,
                                        uint32_t sample_rate_hz,
                                        int16_t *echo_left,
                                        int16_t *echo_right,
                                        uint32_t echo_capacity_frames);
void audio_pad_fx_reset(audio_pad_fx_state_t *fx);
void audio_pad_fx_set(audio_pad_fx_state_t *fx, audio_pad_fx_config_t config);
audio_mixer_frame_t audio_pad_fx_process_frame(audio_pad_fx_state_t *fx,
                                               audio_mixer_frame_t in);
bool audio_pad_fx_is_active(const audio_pad_fx_state_t *fx);
audio_pad_fx_kind_t audio_pad_fx_kind(const audio_pad_fx_state_t *fx);
