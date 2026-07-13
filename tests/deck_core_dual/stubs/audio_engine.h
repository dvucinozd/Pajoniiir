#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_EQ_BAND_LOW = 0,
    AUDIO_EQ_BAND_MID,
    AUDIO_EQ_BAND_HIGH,
    AUDIO_EQ_BAND_COUNT,
} audio_eq_band_t;

extern esp_err_t audio_engine_stub_deck_play_result[2];
extern bool audio_engine_stub_deck_playing[2];
extern bool audio_engine_stub_deck_loaded[2];
extern uint32_t audio_engine_stub_deck_position_ms[2];
extern int audio_engine_stub_deck_seek_count[2];
extern bool audio_engine_stub_loop_active[2];
extern uint32_t audio_engine_stub_loop_start_ms[2];
extern uint32_t audio_engine_stub_loop_end_ms[2];
extern int audio_engine_stub_loop_set_count[2];
extern int audio_engine_stub_loop_clear_count[2];
extern float audio_engine_stub_pitch_percent[2];
extern int audio_engine_stub_pitch_percent_set_count[2];
extern int audio_engine_stub_jog_nudge_count[2];
extern int audio_engine_stub_jog_nudge_last_delta[2];
extern int audio_engine_stub_hold_set_count[2];
extern bool audio_engine_stub_hold[2];

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

static inline void audio_engine_deck_set_master_tempo(uint8_t deck, bool enabled)
{
    (void)deck;
    (void)enabled;
}

static inline void audio_engine_deck_jog_nudge(uint8_t deck, int16_t delta)
{
    if (deck >= 2) return;
    audio_engine_stub_jog_nudge_count[deck]++;
    audio_engine_stub_jog_nudge_last_delta[deck] = delta;
}

static inline void audio_engine_deck_set_hold(uint8_t deck, bool held)
{
    if (deck >= 2) return;
    audio_engine_stub_hold_set_count[deck]++;
    audio_engine_stub_hold[deck] = held;
}

/* Scratch API (vinyl mode Phase 4). Unused in the default host build
 * (CONFIG_AUDIO_SCRATCH_ENABLED off -> deck_core takes the Phase 1 hold path);
 * present so a scratch-enabled compile of deck_core still links. */
extern int audio_engine_stub_scratch_begin_count[2];
extern int audio_engine_stub_scratch_move_count[2];
extern int audio_engine_stub_scratch_move_last_delta[2];
extern int audio_engine_stub_scratch_end_count[2];
extern bool audio_engine_stub_scratch_available[2];

static inline bool audio_engine_deck_scratch_begin(uint8_t deck)
{
    if (deck >= 2) return false;
    audio_engine_stub_scratch_begin_count[deck]++;
    return audio_engine_stub_scratch_available[deck];
}

static inline void audio_engine_deck_scratch_move(uint8_t deck, int16_t delta)
{
    if (deck >= 2) return;
    audio_engine_stub_scratch_move_count[deck]++;
    audio_engine_stub_scratch_move_last_delta[deck] = delta;
}

static inline void audio_engine_deck_scratch_end(uint8_t deck)
{
    if (deck >= 2) return;
    audio_engine_stub_scratch_end_count[deck]++;
}

static inline uint32_t audio_engine_deck_position_ms(uint8_t deck)
{
    return deck < 2 ? audio_engine_stub_deck_position_ms[deck] : 0;
}

static inline bool audio_engine_deck_is_playing(uint8_t deck)
{
    return deck < 2 ? audio_engine_stub_deck_playing[deck] : false;
}

typedef struct {
    bool loaded;
    bool loading;
    uint8_t load_progress;
    char title[64];
    char artist[64];
    char last_error[64];
} audio_engine_deck_status_t;

static inline esp_err_t audio_engine_deck_get_status(uint8_t deck,
                                                     audio_engine_deck_status_t *out)
{
    if (deck >= 2 || !out) return ESP_ERR_INVALID_ARG;
    out->loaded = audio_engine_stub_deck_loaded[deck];
    out->loading = false;
    out->load_progress = audio_engine_stub_deck_loaded[deck] ? 100 : 0;
    out->title[0] = '\0';
    out->artist[0] = '\0';
    out->last_error[0] = '\0';
    return ESP_OK;
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
extern int audio_engine_stub_pregain[2];
extern int audio_engine_stub_master_volume;
extern int audio_engine_stub_headphone_mix;
extern int audio_engine_stub_headphone_level;
extern int audio_engine_stub_master_cue_toggle_count;
extern bool audio_engine_stub_master_cue_enabled;
extern int audio_engine_stub_crossfader;
extern int audio_engine_stub_pfl_toggle_count[2];
extern int audio_engine_stub_eq_raw[2][AUDIO_EQ_BAND_COUNT];
extern int audio_engine_stub_eq_set_count[2][AUDIO_EQ_BAND_COUNT];
extern int audio_engine_stub_filter_raw[2];
extern int audio_engine_stub_filter_set_count[2];
extern int audio_engine_stub_beat_fx_filter_target;
extern int audio_engine_stub_beat_fx_filter_depth;
extern bool audio_engine_stub_beat_fx_filter_enabled;
extern int audio_engine_stub_beat_fx_filter_set_count;
extern int audio_engine_stub_beat_fx_echo_target;
extern int audio_engine_stub_beat_fx_echo_depth;
extern uint32_t audio_engine_stub_beat_fx_echo_delay_ms;
extern bool audio_engine_stub_beat_fx_echo_enabled;
extern int audio_engine_stub_beat_fx_echo_set_count;
extern int audio_engine_stub_beat_fx_flanger_target;
extern int audio_engine_stub_beat_fx_flanger_depth;
extern uint32_t audio_engine_stub_beat_fx_flanger_period_ms;
extern bool audio_engine_stub_beat_fx_flanger_enabled;
extern int audio_engine_stub_beat_fx_flanger_set_count;
extern int audio_engine_stub_pad_fx_deck;
extern int audio_engine_stub_pad_fx_mode;
extern int audio_engine_stub_pad_fx_pad;
extern bool audio_engine_stub_pad_fx_active;
extern int audio_engine_stub_pad_fx_set_count;
extern bool audio_engine_stub_smart_cfx_enabled;
extern bool audio_engine_stub_smart_fader_enabled;

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

static inline esp_err_t audio_engine_set_pregain(uint8_t deck, uint16_t raw_pregain)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_pregain[deck] = raw_pregain;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_master_volume(uint16_t raw_volume)
{
    audio_engine_stub_master_volume = raw_volume;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_headphone_mix(uint16_t raw_mix)
{
    audio_engine_stub_headphone_mix = raw_mix;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_headphone_level(uint16_t raw_level)
{
    audio_engine_stub_headphone_level = raw_level;
    return ESP_OK;
}

static inline esp_err_t audio_engine_toggle_master_cue(void)
{
    audio_engine_stub_master_cue_enabled = !audio_engine_stub_master_cue_enabled;
    audio_engine_stub_master_cue_toggle_count++;
    return ESP_OK;
}

static inline bool audio_engine_get_master_cue_enabled(void)
{
    return audio_engine_stub_master_cue_enabled;
}

static inline esp_err_t audio_engine_set_eq(uint8_t deck, audio_eq_band_t band, uint16_t raw)
{
    if (deck >= 2 || band >= AUDIO_EQ_BAND_COUNT) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_eq_raw[deck][band] = raw;
    audio_engine_stub_eq_set_count[deck][band]++;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_filter(uint8_t deck, uint16_t raw)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_filter_raw[deck] = raw;
    audio_engine_stub_filter_set_count[deck]++;
    return ESP_OK;
}

typedef enum {
    AUDIO_ENGINE_BEAT_FX_TARGET_CH1 = 0,
    AUDIO_ENGINE_BEAT_FX_TARGET_CH2 = 1,
    AUDIO_ENGINE_BEAT_FX_TARGET_BOTH = 2,
} audio_engine_beat_fx_target_t;

static inline esp_err_t audio_engine_set_beat_fx_filter(audio_engine_beat_fx_target_t target,
                                                        uint8_t depth,
                                                        bool enabled)
{
    audio_engine_stub_beat_fx_filter_target = (int)target;
    audio_engine_stub_beat_fx_filter_depth = (int)depth;
    audio_engine_stub_beat_fx_filter_enabled = enabled;
    audio_engine_stub_beat_fx_filter_set_count++;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                                      uint8_t depth,
                                                      uint32_t delay_ms,
                                                      bool enabled)
{
    audio_engine_stub_beat_fx_echo_target = (int)target;
    audio_engine_stub_beat_fx_echo_depth = (int)depth;
    audio_engine_stub_beat_fx_echo_delay_ms = delay_ms;
    audio_engine_stub_beat_fx_echo_enabled = enabled;
    audio_engine_stub_beat_fx_echo_set_count++;
    return ESP_OK;
}

static inline esp_err_t audio_engine_set_beat_fx_flanger(audio_engine_beat_fx_target_t target,
                                                         uint8_t depth,
                                                         uint32_t period_ms,
                                                         bool enabled)
{
    audio_engine_stub_beat_fx_flanger_target = (int)target;
    audio_engine_stub_beat_fx_flanger_depth = (int)depth;
    audio_engine_stub_beat_fx_flanger_period_ms = period_ms;
    audio_engine_stub_beat_fx_flanger_enabled = enabled;
    audio_engine_stub_beat_fx_flanger_set_count++;
    return ESP_OK;
}

typedef enum {
    AUDIO_PAD_FX_MODE_PAD_FX1 = 0,
    AUDIO_PAD_FX_MODE_PAD_FX2 = 1,
} audio_pad_fx_mode_t;

static inline esp_err_t audio_engine_set_pad_fx(uint8_t deck,
                                                audio_pad_fx_mode_t mode,
                                                uint8_t pad,
                                                bool active)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_pad_fx_deck = (int)deck;
    audio_engine_stub_pad_fx_mode = (int)mode;
    audio_engine_stub_pad_fx_pad = (int)pad;
    audio_engine_stub_pad_fx_active = active;
    audio_engine_stub_pad_fx_set_count++;
    return ESP_OK;
}

static inline esp_err_t audio_engine_toggle_pfl(uint8_t deck)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_pfl_toggle_count[deck]++;
    return ESP_OK;
}

static inline esp_err_t audio_engine_toggle_smart_cfx(void)
{
    audio_engine_stub_smart_cfx_enabled = !audio_engine_stub_smart_cfx_enabled;
    return ESP_OK;
}

static inline bool audio_engine_get_smart_cfx_enabled(void)
{
    return audio_engine_stub_smart_cfx_enabled;
}

static inline esp_err_t audio_engine_toggle_smart_fader(void)
{
    audio_engine_stub_smart_fader_enabled = !audio_engine_stub_smart_fader_enabled;
    return ESP_OK;
}

static inline bool audio_engine_get_smart_fader_enabled(void)
{
    return audio_engine_stub_smart_fader_enabled;
}

static inline bool audio_engine_get_pfl_enabled(uint8_t deck)
{
    if (deck >= 2) return false;
    return (audio_engine_stub_pfl_toggle_count[deck] % 2) != 0;
}
