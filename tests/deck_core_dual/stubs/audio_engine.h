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

extern esp_err_t audio_engine_stub_deck_play_result[2];
extern bool audio_engine_stub_deck_playing[2];
extern uint32_t audio_engine_stub_deck_position_ms[2];
extern int audio_engine_stub_deck_seek_count[2];
extern bool audio_engine_stub_loop_active[2];
extern uint32_t audio_engine_stub_loop_start_ms[2];
extern uint32_t audio_engine_stub_loop_end_ms[2];
extern int audio_engine_stub_loop_set_count[2];
extern int audio_engine_stub_loop_clear_count[2];
extern float audio_engine_stub_pitch_percent[2];
extern int audio_engine_stub_pitch_percent_set_count[2];

static inline esp_err_t audio_engine_deck_play(uint8_t deck)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = audio_engine_stub_deck_play_result[deck];
    if (rc == ESP_OK) {
        audio_engine_stub_deck_playing[deck] = true;
    }
    return rc;
}

static inline esp_err_t audio_engine_deck_pause(uint8_t deck)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_deck_playing[deck] = false;
    return ESP_OK;
}

static inline esp_err_t audio_engine_deck_stop(uint8_t deck)
{
    return deck == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static inline esp_err_t audio_engine_deck_seek(uint8_t deck, uint32_t position_ms)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_deck_seek_count[deck]++;
    audio_engine_stub_deck_position_ms[deck] = position_ms;
    return ESP_OK;
}

static inline void audio_engine_deck_set_pitch(uint8_t deck, int16_t raw_pitch)
{
    (void)deck;
    (void)raw_pitch;
}

static inline void audio_engine_deck_set_pitch_percent(uint8_t deck, float percent)
{
    if (deck >= 2) return;
    audio_engine_stub_pitch_percent[deck] = percent;
    audio_engine_stub_pitch_percent_set_count[deck]++;
}

static inline uint32_t audio_engine_deck_position_ms(uint8_t deck)
{
    return deck < 2 ? audio_engine_stub_deck_position_ms[deck] : 0;
}

static inline bool audio_engine_deck_is_playing(uint8_t deck)
{
    return deck < 2 ? audio_engine_stub_deck_playing[deck] : false;
}

static inline esp_err_t audio_engine_deck_get_loop_state(uint8_t deck,
                                                        bool *active,
                                                        uint32_t *start_ms,
                                                        uint32_t *end_ms)
{
    if (deck >= 2 || !active || !start_ms || !end_ms) return ESP_ERR_INVALID_ARG;
    *active = audio_engine_stub_loop_active[deck];
    *start_ms = audio_engine_stub_loop_start_ms[deck];
    *end_ms = audio_engine_stub_loop_end_ms[deck];
    return ESP_OK;
}

static inline esp_err_t audio_engine_deck_set_loop(uint8_t deck,
                                                   uint32_t start_ms,
                                                   uint32_t end_ms)
{
    if (deck >= 2 || end_ms <= start_ms) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_loop_active[deck] = true;
    audio_engine_stub_loop_start_ms[deck] = start_ms;
    audio_engine_stub_loop_end_ms[deck] = end_ms;
    audio_engine_stub_loop_set_count[deck]++;
    return ESP_OK;
}

static inline esp_err_t audio_engine_deck_clear_loop(uint8_t deck)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_loop_active[deck] = false;
    audio_engine_stub_loop_clear_count[deck]++;
    return ESP_OK;
}

extern int audio_engine_stub_channel_volume[2];
extern int audio_engine_stub_crossfader;
extern int audio_engine_stub_pfl_toggle_count[2];

static inline esp_err_t audio_engine_set_channel_volume(uint8_t deck, uint16_t raw_volume)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_channel_volume[deck] = raw_volume;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_crossfader(uint16_t raw_crossfader)
{
    audio_engine_stub_crossfader = raw_crossfader;
    return ESP_OK;
}

static inline esp_err_t audio_engine_toggle_pfl(uint8_t deck)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_pfl_toggle_count[deck]++;
    return ESP_OK;
}

static inline bool audio_engine_get_pfl_enabled(uint8_t deck)
{
    if (deck >= 2) return false;
    return (audio_engine_stub_pfl_toggle_count[deck] % 2) != 0;
}
