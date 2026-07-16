#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"

#define AUDIO_FILTER_RAW_MIN    0u
#define AUDIO_FILTER_RAW_CENTER AUDIO_MIXER_CONTROL_CENTER
#define AUDIO_FILTER_RAW_MAX    AUDIO_MIXER_CONTROL_MAX

typedef struct {
    uint16_t raw;
    uint32_t sample_rate_hz;
    /* Knob smoothing + cached ZDF SVF coefficients, refreshed once per
     * coefficient block (not per sample) to keep tanf/expf off the hot path. */
    float smoothed_raw;
    uint32_t block_frames_left;
    bool coefficients_dirty;
    bool bypassed;
    bool hp_mode;
    float k;
    float a1;
    float a2;
    float a3;
    /* Integrator state, one pair per channel. */
    float ic1eq[2];
    float ic2eq[2];
} audio_filter_state_t;

void audio_filter_init(audio_filter_state_t *filter, uint32_t sample_rate_hz);
void audio_filter_set_sample_rate(audio_filter_state_t *filter, uint32_t sample_rate_hz);
void audio_filter_reset(audio_filter_state_t *filter);
void audio_filter_set_raw(audio_filter_state_t *filter, uint16_t raw);
uint16_t audio_filter_get_raw(const audio_filter_state_t *filter);
audio_mixer_frame_t audio_filter_process_frame(audio_filter_state_t *filter,
                                               bool enabled,
                                               audio_mixer_frame_t in);
