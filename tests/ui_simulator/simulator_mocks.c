#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lvgl.h"
#include "esp_err.h"
#include "audio_engine.h"
#include "app_settings.h"
#include "control_link.h"

esp_err_t audio_engine_stub_deck_play_result[2] = {ESP_OK, ESP_OK};
bool audio_engine_stub_deck_playing[2] = {false, false};
bool audio_engine_stub_deck_loaded[2] = {true, true};
uint32_t audio_engine_stub_deck_position_ms[2] = {45250u, 91800u};
int audio_engine_stub_deck_seek_count[2] = {0, 0};
bool audio_engine_stub_loop_active[2] = {false, false};
uint32_t audio_engine_stub_loop_start_ms[2] = {0, 0};
uint32_t audio_engine_stub_loop_end_ms[2] = {0, 0};
int audio_engine_stub_loop_set_count[2] = {0, 0};
int audio_engine_stub_loop_clear_count[2] = {0, 0};
float audio_engine_stub_pitch_percent[2] = {0.0f, 0.0f};
int audio_engine_stub_pitch_percent_set_count[2] = {0, 0};
int audio_engine_stub_jog_nudge_count[2] = {0, 0};
int audio_engine_stub_jog_nudge_last_delta[2] = {0, 0};
int audio_engine_stub_hold_set_count[2] = {0, 0};
bool audio_engine_stub_hold[2] = {false, false};
int audio_engine_stub_scratch_begin_count[2] = {0, 0};
int audio_engine_stub_scratch_move_count[2] = {0, 0};
int audio_engine_stub_scratch_move_last_delta[2] = {0, 0};
int audio_engine_stub_scratch_end_count[2] = {0, 0};
bool audio_engine_stub_scratch_available[2] = {true, true};

int audio_engine_stub_channel_volume[2] = {16383, 13800};
int audio_engine_stub_pregain[2] = {8192, 8192};
int audio_engine_stub_master_volume = 12288;
int audio_engine_stub_headphone_mix = 8192;
int audio_engine_stub_headphone_level = 10000;
int audio_engine_stub_master_cue_toggle_count = 0;
bool audio_engine_stub_master_cue_enabled = false;
int audio_engine_stub_crossfader = 8192;
int audio_engine_stub_pfl_toggle_count[2] = {1, 0};
int audio_engine_stub_eq_raw[2][AUDIO_EQ_BAND_COUNT] = {{8192, 8192, 8192},
                                                        {8192, 8192, 8192}};
int audio_engine_stub_eq_set_count[2][AUDIO_EQ_BAND_COUNT] = {{0}};
int audio_engine_stub_filter_raw[2] = {8192, 8192};
int audio_engine_stub_filter_set_count[2] = {0, 0};
int audio_engine_stub_beat_fx_filter_target = 0;
int audio_engine_stub_beat_fx_filter_depth = 0;
bool audio_engine_stub_beat_fx_filter_enabled = false;
int audio_engine_stub_beat_fx_filter_set_count = 0;
int audio_engine_stub_beat_fx_echo_target = 0;
int audio_engine_stub_beat_fx_echo_depth = 0;
uint32_t audio_engine_stub_beat_fx_echo_delay_ms = 0;
bool audio_engine_stub_beat_fx_echo_enabled = false;
int audio_engine_stub_beat_fx_echo_set_count = 0;
int audio_engine_stub_beat_fx_delay_target = 0;
int audio_engine_stub_beat_fx_delay_depth = 0;
uint32_t audio_engine_stub_beat_fx_delay_delay_ms = 0;
bool audio_engine_stub_beat_fx_delay_enabled = false;
int audio_engine_stub_beat_fx_delay_set_count = 0;
int audio_engine_stub_beat_fx_flanger_target = 0;
int audio_engine_stub_beat_fx_flanger_depth = 0;
uint32_t audio_engine_stub_beat_fx_flanger_period_ms = 0;
bool audio_engine_stub_beat_fx_flanger_enabled = false;
int audio_engine_stub_beat_fx_flanger_set_count = 0;
int audio_engine_stub_pad_fx_deck = 0;
int audio_engine_stub_pad_fx_mode = 0;
int audio_engine_stub_pad_fx_pad = 0;
bool audio_engine_stub_pad_fx_active = false;
int audio_engine_stub_pad_fx_set_count = 0;
bool audio_engine_stub_smart_cfx_enabled = false;
bool audio_engine_stub_smart_fader_enabled = false;

esp_err_t audio_engine_set_master_trim(float gain)
{
    (void)gain;
    return ESP_OK;
}

esp_err_t audio_engine_set_cue_mode(uint8_t mode)
{
    (void)mode;
    return ESP_OK;
}

void audio_engine_clear_loop(void)
{
}

esp_err_t audio_engine_deck_load(uint8_t deck, const char *path,
                                 const uint32_t *pvbr, uint32_t duration_ms)
{
    (void)path;
    (void)pvbr;
    (void)duration_ms;
    if (deck >= 2) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_engine_stub_deck_loaded[deck] = true;
    audio_engine_stub_deck_position_ms[deck] = 0;
    audio_engine_stub_deck_playing[deck] = false;
    audio_engine_stub_loop_active[deck] = false;
    return ESP_OK;
}

ae_state_t audio_engine_get_state(void)
{
    return AE_READY;
}

uint8_t audio_engine_load_progress(void)
{
    return 100;
}

void audio_engine_get_mixer_snapshot(audio_engine_mixer_snapshot_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->channel_volume[0] = (uint16_t)audio_engine_stub_channel_volume[0];
    out->channel_volume[1] = (uint16_t)audio_engine_stub_channel_volume[1];
    out->crossfader = (uint16_t)audio_engine_stub_crossfader;
    out->master_trim = 1.0f;
    out->master_volume = (uint16_t)audio_engine_stub_master_volume;
    out->headphone_mix = (uint16_t)audio_engine_stub_headphone_mix;
    out->headphone_level = (uint16_t)audio_engine_stub_headphone_level;
    out->pfl_enabled[0] = true;
    out->pfl_enabled[1] = false;
    out->deck_peak_display[0] = 9100;
    out->deck_peak_display[1] = 6400;
}

uint16_t audio_engine_get_deck_peak(uint8_t deck)
{
    return deck == 0 ? 9100u : 6400u;
}

esp_err_t audio_engine_stop_all(void)
{
    audio_engine_stub_deck_playing[0] = false;
    audio_engine_stub_deck_playing[1] = false;
    return ESP_OK;
}

esp_err_t audio_engine_last_error(void)
{
    return ESP_OK;
}

const char *audio_engine_last_error_text(void)
{
    return "";
}

static app_settings_t s_settings = {
    .audio_out = 0,
    .backlight_pct = 80,
    .time_remain = 1,
    .cue_mode = 0,
    .master_trim_preset = 0,
    .wifi_remote = 0,
};

esp_err_t app_settings_init(void)
{
    return ESP_OK;
}

app_settings_t app_settings_get(void)
{
    return s_settings;
}

void app_settings_set_audio_out(uint8_t value) { s_settings.audio_out = value; }
void app_settings_set_backlight(uint8_t value) { s_settings.backlight_pct = value; }
void app_settings_set_time_remain(uint8_t value) { s_settings.time_remain = value; }
void app_settings_set_cue_mode(uint8_t value) { s_settings.cue_mode = value; }
void app_settings_set_master_trim_preset(uint8_t value) { s_settings.master_trim_preset = value; }
void app_settings_set_wifi_remote(uint8_t value) { s_settings.wifi_remote = value; }

int64_t esp_timer_get_time(void)
{
    return (int64_t)lv_tick_get() * 1000;
}

extern void deck_core_test_apply_event(const ctrl_event_t *event);

void ui_simulator_deck_set_position(uint32_t position_ms)
{
    audio_engine_stub_deck_position_ms[0] = position_ms;
    audio_engine_stub_deck_position_ms[1] = position_ms;
}

void ui_simulator_deck_set_playing(bool playing)
{
    audio_engine_stub_deck_playing[0] = playing;
    audio_engine_stub_deck_playing[1] = playing;
}

void ui_simulator_deck_toggle_play(void)
{
    ctrl_event_t event = {
        .type = CTRL_EV_BUTTON,
        .id = BTN_PLAY,
        .value = 1,
        .deck = CTRL_DECK_1,
    };
    deck_core_test_apply_event(&event);
}

void ui_simulator_deck_toggle_master_tempo(void)
{
    ctrl_event_t event = {
        .type = CTRL_EV_BUTTON,
        .id = BTN_MASTER_TEMPO,
        .value = 1,
        .deck = CTRL_DECK_1,
    };
    deck_core_test_apply_event(&event);
}

void ui_lvgl_lock(void) {}
void ui_lvgl_unlock(void) {}
void media_io_gate_begin(void) {}
void media_io_gate_end(void) {}
