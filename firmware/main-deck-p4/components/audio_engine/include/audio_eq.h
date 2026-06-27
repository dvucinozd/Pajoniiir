#pragma once

#include <stdint.h>
#include "audio_mixer.h"

#define AUDIO_EQ_RAW_MIN    0u
#define AUDIO_EQ_RAW_CENTER AUDIO_MIXER_CONTROL_CENTER
#define AUDIO_EQ_RAW_MAX    AUDIO_MIXER_CONTROL_MAX

typedef enum {
    AUDIO_EQ_BAND_LOW = 0,
    AUDIO_EQ_BAND_MID,
    AUDIO_EQ_BAND_HIGH,
    AUDIO_EQ_BAND_COUNT,
} audio_eq_band_t;

typedef struct {
    float low_lp[2];
    float low_lp2[2];
    float high_lp[2];
    float high_lp2[2];
    float low_alpha;
    float high_alpha;
    uint16_t raw[AUDIO_EQ_BAND_COUNT];
    float gain[AUDIO_EQ_BAND_COUNT];
} audio_eq_state_t;

void audio_eq_init(audio_eq_state_t *eq, uint32_t sample_rate_hz);
void audio_eq_reset_filters(audio_eq_state_t *eq);
void audio_eq_set_raw(audio_eq_state_t *eq,
                      uint16_t low,
                      uint16_t mid,
                      uint16_t high);
void audio_eq_set_band_raw(audio_eq_state_t *eq, audio_eq_band_t band, uint16_t raw);
uint16_t audio_eq_get_band_raw(const audio_eq_state_t *eq, audio_eq_band_t band);
float audio_eq_raw_to_gain(uint16_t raw);
audio_mixer_frame_t audio_eq_process_frame(audio_eq_state_t *eq, audio_mixer_frame_t in);
