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

static inline esp_err_t audio_engine_deck_play(uint8_t deck)
{
    return deck == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static inline esp_err_t audio_engine_deck_pause(uint8_t deck)
{
    return deck == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static inline esp_err_t audio_engine_deck_stop(uint8_t deck)
{
    return deck == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static inline esp_err_t audio_engine_deck_seek(uint8_t deck, uint32_t position_ms)
{
    (void)position_ms;
    return deck == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static inline void audio_engine_deck_set_pitch(uint8_t deck, int16_t raw_pitch)
{
    (void)deck;
    (void)raw_pitch;
}

static inline uint32_t audio_engine_deck_position_ms(uint8_t deck)
{
    (void)deck;
    return 0;
}

static inline bool audio_engine_deck_is_playing(uint8_t deck)
{
    (void)deck;
    return false;
}
