#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

static inline bool audio_engine_is_playing(void) { return false; }
static inline esp_err_t audio_engine_play(void) { return ESP_OK; }
static inline esp_err_t audio_engine_pause(void) { return ESP_OK; }
static inline esp_err_t audio_engine_stop(void) { return ESP_OK; }
static inline esp_err_t audio_engine_seek(uint32_t position_ms)
{
    (void)position_ms;
    return ESP_OK;
}
static inline void audio_engine_set_pitch(int16_t raw_pitch)
{
    (void)raw_pitch;
}
static inline uint32_t audio_engine_position_ms(void) { return 0; }
