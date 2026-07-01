#include "deck_core.h"
#include "control_link.h"
#include "hot_cue_store.h"
#include "audio_engine.h"
#include "rekordbox_anlz.h"
#include <assert.h>
#include <stdio.h>

static int s_load_calls[DECK_CORE_DECK_COUNT];
static int s_browse_delta;
static int s_toggle_library_view_calls;
static uint32_t s_loaded_track_key[DECK_CORE_DECK_COUNT];
static const anlz_metadata_t *s_loaded_anlz[DECK_CORE_DECK_COUNT];
static uint16_t s_loaded_bpm[DECK_CORE_DECK_COUNT];
int audio_engine_stub_channel_volume[DECK_CORE_DECK_COUNT];
int audio_engine_stub_crossfader;
int audio_engine_stub_pfl_toggle_count[DECK_CORE_DECK_COUNT];
int audio_engine_stub_eq_raw[DECK_CORE_DECK_COUNT][AUDIO_EQ_BAND_COUNT];
int audio_engine_stub_eq_set_count[DECK_CORE_DECK_COUNT][AUDIO_EQ_BAND_COUNT];
int audio_engine_stub_filter_raw[DECK_CORE_DECK_COUNT];
int audio_engine_stub_filter_set_count[DECK_CORE_DECK_COUNT];
int audio_engine_stub_beat_fx_filter_target;
int audio_engine_stub_beat_fx_filter_depth;
bool audio_engine_stub_beat_fx_filter_enabled;
int audio_engine_stub_beat_fx_filter_set_count;
int audio_engine_stub_beat_fx_echo_target;
int audio_engine_stub_beat_fx_echo_depth;
uint32_t audio_engine_stub_beat_fx_echo_delay_ms;
bool audio_engine_stub_beat_fx_echo_enabled;
int audio_engine_stub_beat_fx_echo_set_count;
int audio_engine_stub_pad_fx_deck;
int audio_engine_stub_pad_fx_mode;
int audio_engine_stub_pad_fx_pad;
bool audio_engine_stub_pad_fx_active;
int audio_engine_stub_pad_fx_set_count;
bool audio_engine_stub_smart_cfx_enabled;
bool audio_engine_stub_smart_fader_enabled;
esp_err_t audio_engine_stub_deck_play_result[DECK_CORE_DECK_COUNT];
bool audio_engine_stub_deck_playing[DECK_CORE_DECK_COUNT];
uint32_t audio_engine_stub_deck_position_ms[DECK_CORE_DECK_COUNT];
int audio_engine_stub_deck_seek_count[DECK_CORE_DECK_COUNT];
bool audio_engine_stub_loop_active[DECK_CORE_DECK_COUNT];
uint32_t audio_engine_stub_loop_start_ms[DECK_CORE_DECK_COUNT];
uint32_t audio_engine_stub_loop_end_ms[DECK_CORE_DECK_COUNT];
int audio_engine_stub_loop_set_count[DECK_CORE_DECK_COUNT];
int audio_engine_stub_loop_clear_count[DECK_CORE_DECK_COUNT];
float audio_engine_stub_pitch_percent[DECK_CORE_DECK_COUNT];
int audio_engine_stub_pitch_percent_set_count[DECK_CORE_DECK_COUNT];
extern int control_link_stub_led_count;
void control_link_stub_reset_leds(void);
int control_link_stub_last_led_state(led_id_t led, uint8_t deck);

esp_err_t ui_library_load_selected_for_deck(uint8_t deck)
{
    assert(deck < DECK_CORE_DECK_COUNT);
    s_load_calls[deck]++;
    return ESP_OK;
}

esp_err_t ui_library_select_delta(int delta)
{
    s_browse_delta += delta;
    return ESP_OK;
}

uint32_t ui_library_loaded_track_key_for_deck(uint8_t deck)
{
    assert(deck < DECK_CORE_DECK_COUNT);
    return s_loaded_track_key[deck];
}

const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck)
{
    assert(deck < DECK_CORE_DECK_COUNT);
    return s_loaded_anlz[deck];
}

uint16_t ui_library_deck_bpm(uint8_t deck, uint16_t fallback_bpm)
{
    assert(deck < DECK_CORE_DECK_COUNT);
    return s_loaded_bpm[deck] > 0 ? s_loaded_bpm[deck] : fallback_bpm;
}

esp_err_t ui_toggle_library_view(void)
{
    s_toggle_library_view_calls++;
    return ESP_OK;
}

static ctrl_event_t deck_button(uint8_t id)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BUTTON,
        .id = id,
        .value = 1,
    };
}

static ctrl_event_t browser_button(uint8_t id)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BUTTON,
        .id = id,
        .value = 1,
    };
}

static ctrl_event_t browse_delta(int16_t delta)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BROWSE,
        .id = CTRL_ID_BROWSE_DELTA,
        .value = delta,
    };
}

static ctrl_event_t deck_pitch(uint8_t id, int16_t value)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_PITCH,
        .id = id,
        .value = value,
    };
}

static ctrl_event_t mixer_value(uint8_t id, int16_t value)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_PITCH,
        .id = id,
        .value = value,
    };
}

static ctrl_event_t mixer_button(uint8_t id, int16_t value)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BUTTON,
        .id = id,
        .value = value,
    };
}

static ctrl_event_t flx4_connection_state(int16_t value)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_STATE,
        .id = CTRL_ID_FLX4_CONNECTION,
        .value = value,
    };
}

static ctrl_event_t beat_fx_button(uint8_t id, int16_t value)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BUTTON,
        .id = id,
        .value = value,
    };
}

static ctrl_event_t beat_fx_depth(int16_t value)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_PITCH,
        .id = CTRL_ID_BEAT_FX_DEPTH,
        .value = value,
    };
}

static void reset_audio_engine_stub(void)
{
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        s_loaded_track_key[deck] = 0;
        s_loaded_anlz[deck] = NULL;
        s_loaded_bpm[deck] = 0;
        audio_engine_stub_deck_play_result[deck] = ESP_OK;
        audio_engine_stub_deck_playing[deck] = false;
        audio_engine_stub_deck_position_ms[deck] = 0;
        audio_engine_stub_deck_seek_count[deck] = 0;
        audio_engine_stub_loop_active[deck] = false;
        audio_engine_stub_loop_start_ms[deck] = 0;
        audio_engine_stub_loop_end_ms[deck] = 0;
        audio_engine_stub_loop_set_count[deck] = 0;
        audio_engine_stub_loop_clear_count[deck] = 0;
        audio_engine_stub_pitch_percent[deck] = 0.0f;
        audio_engine_stub_pitch_percent_set_count[deck] = 0;
        audio_engine_stub_filter_raw[deck] = -1;
        audio_engine_stub_filter_set_count[deck] = 0;
    }
    audio_engine_stub_beat_fx_filter_target = -1;
    audio_engine_stub_beat_fx_filter_depth = -1;
    audio_engine_stub_beat_fx_filter_enabled = false;
    audio_engine_stub_beat_fx_filter_set_count = 0;
    audio_engine_stub_beat_fx_echo_target = -1;
    audio_engine_stub_beat_fx_echo_depth = -1;
    audio_engine_stub_beat_fx_echo_delay_ms = 0;
    audio_engine_stub_beat_fx_echo_enabled = false;
    audio_engine_stub_beat_fx_echo_set_count = 0;
    audio_engine_stub_pad_fx_deck = -1;
    audio_engine_stub_pad_fx_mode = -1;
    audio_engine_stub_pad_fx_pad = -1;
    audio_engine_stub_pad_fx_active = false;
    audio_engine_stub_pad_fx_set_count = 0;
    audio_engine_stub_smart_cfx_enabled = false;
    audio_engine_stub_smart_fader_enabled = false;
}

static void clear_test_hot_cues(void)
{
    (void)hot_cue_store_clear(1001);
    (void)hot_cue_store_clear(2002);
    (void)hot_cue_store_clear(3003);
}

static void test_decks_track_transport_independently(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck1_play = deck_button(CTRL_ID_DECK1_PLAY);
    ctrl_event_t deck2_play = deck_button(CTRL_ID_DECK2_PLAY);
    ctrl_event_t deck2_cue = deck_button(CTRL_ID_DECK2_CUE);

    deck_core_test_apply_event(&deck1_play);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).playing);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);

    deck_core_test_apply_event(&deck2_play);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).playing);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).playing);

    deck_core_test_apply_event(&deck2_cue);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).playing);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);
}

static void test_deck2_snapshot_follows_audio_engine_position(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck2_play = deck_button(CTRL_ID_DECK2_PLAY);
    deck_core_test_apply_event(&deck2_play);
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 4321;

    deck_state_t deck2 = deck_core_test_get_deck_state(CTRL_DECK_2);

    assert(deck2.playing);
    assert(deck2.position_ms == 4321);
}

static void test_failed_deck_play_does_not_mark_deck_playing(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_play_result[CTRL_DECK_2] = ESP_ERR_NOT_SUPPORTED;

    ctrl_event_t deck2_play = deck_button(CTRL_ID_DECK2_PLAY);

    deck_core_test_apply_event(&deck2_play);

    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);
}

static void test_decks_track_pitch_independently(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck1_pitch = deck_pitch(CTRL_ID_DECK1_TEMPO, 7000);
    ctrl_event_t deck2_pitch = deck_pitch(CTRL_ID_DECK2_TEMPO, 9600);

    deck_core_test_apply_event(&deck1_pitch);
    deck_core_test_apply_event(&deck2_pitch);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch == 7000);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).pitch == 9600);
}

static void test_tempo_range_defaults_to_ten_percent(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    deck_state_t deck2 = deck_core_test_get_deck_state(CTRL_DECK_2);

    assert(deck1.tempo_range_percent == 10);
    assert(deck2.tempo_range_percent == 10);
    assert(deck1.pitch_centipercent == 0);
    assert(deck2.pitch_centipercent == 0);
}

static void test_tempo_range_button_cycles_requested_deck_only(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck1_range = deck_button(CTRL_ID_DECK1_TEMPO_RANGE);
    ctrl_event_t deck2_range = deck_button(CTRL_ID_DECK2_TEMPO_RANGE);

    deck_core_test_apply_event(&deck1_range);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 16);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).tempo_range_percent == 10);

    deck_core_test_apply_event(&deck1_range);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 6);

    deck_core_test_apply_event(&deck1_range);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 10);

    deck_core_test_apply_event(&deck2_range);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 10);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).tempo_range_percent == 16);
}

static void test_tempo_range_release_does_not_cycle(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_TEMPO_RANGE);
    release.value = 0;
    deck_core_test_apply_event(&release);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 10);
    assert(audio_engine_stub_pitch_percent_set_count[CTRL_DECK_1] == 0);
}

static void test_pitch_mapping_uses_selected_tempo_range(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck1_min = deck_pitch(CTRL_ID_DECK1_TEMPO, 0);
    ctrl_event_t deck2_max = deck_pitch(CTRL_ID_DECK2_TEMPO, 16383);

    deck_core_test_apply_event(&deck1_min);
    deck_core_test_apply_event(&deck2_max);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch_centipercent == 1000);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).pitch_centipercent == -999);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > 9.99f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < 10.01f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_2] < -9.98f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_2] > -10.01f);

    ctrl_event_t range = deck_button(CTRL_ID_DECK1_TEMPO_RANGE);
    deck_core_test_apply_event(&range);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 16);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch_centipercent == 1600);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > 15.99f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < 16.01f);
}

static void test_tempo_range_change_reapplies_current_pitch(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t pitch = deck_pitch(CTRL_ID_DECK1_TEMPO, 4096);
    ctrl_event_t range = deck_button(CTRL_ID_DECK1_TEMPO_RANGE);

    deck_core_test_apply_event(&pitch);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch_centipercent == 500);
    assert(audio_engine_stub_pitch_percent_set_count[CTRL_DECK_1] == 1);

    deck_core_test_apply_event(&range);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).tempo_range_percent == 16);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch_centipercent == 800);
    assert(audio_engine_stub_pitch_percent_set_count[CTRL_DECK_1] == 2);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > 7.99f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < 8.01f);
}

static void test_browser_namespace_routes_load_to_requested_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_load_calls[CTRL_DECK_1] = 0;
    s_load_calls[CTRL_DECK_2] = 0;

    ctrl_event_t load_deck1 = browser_button(CTRL_ID_LOAD_DECK1);
    ctrl_event_t load_deck2 = browser_button(CTRL_ID_LOAD_DECK2);

    deck_core_test_apply_event(&load_deck1);
    deck_core_test_apply_event(&load_deck2);
    deck_core_test_apply_event(&load_deck2);

    assert(s_load_calls[CTRL_DECK_1] == 1);
    assert(s_load_calls[CTRL_DECK_2] == 2);
}

static void test_browser_namespace_routes_browse_delta(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_browse_delta = 0;

    ctrl_event_t browse = browse_delta(3);
    deck_core_test_apply_event(&browse);

    assert(s_browse_delta == 3);
}

static void test_cue_shift_jumps_requested_deck_to_track_start(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_playing[CTRL_DECK_1] = true;
    audio_engine_stub_deck_playing[CTRL_DECK_2] = true;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 12345;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 67890;

    ctrl_event_t deck1_to_start = deck_button(CTRL_ID_DECK1_TO_START);
    ctrl_event_t deck2_to_start = deck_button(CTRL_ID_DECK2_TO_START);

    deck_core_test_apply_event(&deck1_to_start);
    deck_core_test_apply_event(&deck2_to_start);

    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 0);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).playing);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);
}

static void test_browser_press_toggles_library_view_without_loading_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_load_calls[CTRL_DECK_1] = 0;
    s_load_calls[CTRL_DECK_2] = 0;
    s_toggle_library_view_calls = 0;

    ctrl_event_t browse_press = browser_button(CTRL_ID_BROWSE_PRESS);
    ctrl_event_t browse_release = browse_press;
    browse_release.value = 0;

    deck_core_test_apply_event(&browse_press);
    deck_core_test_apply_event(&browse_release);

    assert(s_toggle_library_view_calls == 1);
    assert(s_load_calls[CTRL_DECK_1] == 0);
    assert(s_load_calls[CTRL_DECK_2] == 0);
}

static void test_mixer_namespace_routes_volume_and_crossfader(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_channel_volume[CTRL_DECK_1] = -1;
    audio_engine_stub_channel_volume[CTRL_DECK_2] = -1;
    audio_engine_stub_crossfader = -1;

    ctrl_event_t ch1 = mixer_value(CTRL_ID_CH1_VOLUME, 7000);
    ctrl_event_t ch2 = mixer_value(CTRL_ID_CH2_VOLUME, 9000);
    ctrl_event_t crossfader = mixer_value(CTRL_ID_CROSSFADER, 8192);

    deck_core_test_apply_event(&ch1);
    deck_core_test_apply_event(&ch2);
    deck_core_test_apply_event(&crossfader);

    assert(audio_engine_stub_channel_volume[CTRL_DECK_1] == 7000);
    assert(audio_engine_stub_channel_volume[CTRL_DECK_2] == 9000);
    assert(audio_engine_stub_crossfader == 8192);
}

static void test_mixer_namespace_routes_eq_controls(void)
{
    for (int deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        for (int band = 0; band < AUDIO_EQ_BAND_COUNT; band++) {
            audio_engine_stub_eq_raw[deck][band] = -1;
            audio_engine_stub_eq_set_count[deck][band] = 0;
        }
    }

    ctrl_event_t ch1_low = mixer_value(CTRL_ID_CH1_EQ_LOW, 1000);
    ctrl_event_t ch1_mid = mixer_value(CTRL_ID_CH1_EQ_MID, 2000);
    ctrl_event_t ch1_high = mixer_value(CTRL_ID_CH1_EQ_HIGH, 3000);
    ctrl_event_t ch2_low = mixer_value(CTRL_ID_CH2_EQ_LOW, 4000);
    ctrl_event_t ch2_mid = mixer_value(CTRL_ID_CH2_EQ_MID, 5000);
    ctrl_event_t ch2_high = mixer_value(CTRL_ID_CH2_EQ_HIGH, 6000);

    deck_core_test_apply_event(&ch1_low);
    deck_core_test_apply_event(&ch1_mid);
    deck_core_test_apply_event(&ch1_high);
    deck_core_test_apply_event(&ch2_low);
    deck_core_test_apply_event(&ch2_mid);
    deck_core_test_apply_event(&ch2_high);

    assert(audio_engine_stub_eq_raw[CTRL_DECK_1][AUDIO_EQ_BAND_LOW] == 1000);
    assert(audio_engine_stub_eq_raw[CTRL_DECK_1][AUDIO_EQ_BAND_MID] == 2000);
    assert(audio_engine_stub_eq_raw[CTRL_DECK_1][AUDIO_EQ_BAND_HIGH] == 3000);
    assert(audio_engine_stub_eq_raw[CTRL_DECK_2][AUDIO_EQ_BAND_LOW] == 4000);
    assert(audio_engine_stub_eq_raw[CTRL_DECK_2][AUDIO_EQ_BAND_MID] == 5000);
    assert(audio_engine_stub_eq_raw[CTRL_DECK_2][AUDIO_EQ_BAND_HIGH] == 6000);
    assert(audio_engine_stub_eq_set_count[CTRL_DECK_1][AUDIO_EQ_BAND_LOW] == 1);
    assert(audio_engine_stub_eq_set_count[CTRL_DECK_2][AUDIO_EQ_BAND_HIGH] == 1);
}

static void test_mixer_namespace_routes_filter_controls(void)
{
    reset_audio_engine_stub();

    ctrl_event_t ch1_filter = mixer_value(CTRL_ID_CH1_FILTER, 1234);
    ctrl_event_t ch2_filter = mixer_value(CTRL_ID_CH2_FILTER, 5678);

    deck_core_test_apply_event(&ch1_filter);
    deck_core_test_apply_event(&ch2_filter);

    assert(audio_engine_stub_filter_raw[CTRL_DECK_1] == 1234);
    assert(audio_engine_stub_filter_raw[CTRL_DECK_2] == 5678);
    assert(audio_engine_stub_filter_set_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_filter_set_count[CTRL_DECK_2] == 1);
}

static void test_mixer_namespace_routes_pfl_toggle_on_press(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_pfl_toggle_count[CTRL_DECK_1] = 0;
    audio_engine_stub_pfl_toggle_count[CTRL_DECK_2] = 0;

    ctrl_event_t pfl1_press = mixer_button(CTRL_ID_DECK1_PFL, 1);
    ctrl_event_t pfl1_release = mixer_button(CTRL_ID_DECK1_PFL, 0);
    ctrl_event_t pfl2_press = mixer_button(CTRL_ID_DECK2_PFL, 1);

    deck_core_test_apply_event(&pfl1_press);
    deck_core_test_apply_event(&pfl1_release);
    deck_core_test_apply_event(&pfl2_press);

    assert(audio_engine_stub_pfl_toggle_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_pfl_toggle_count[CTRL_DECK_2] == 1);
}

static void test_sync_button_toggles_requested_deck_sync_led_state(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 120;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    ctrl_event_t deck2_sync = deck_button(CTRL_ID_DECK2_SYNC);

    deck_core_test_apply_event(&deck1_sync);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).sync_enabled);

    deck_core_test_apply_event(&deck2_sync);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).sync_enabled);

    deck_core_test_apply_event(&deck1_sync);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).sync_enabled);
}

static void test_sync_matches_requested_deck_to_other_deck_bpm(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 128;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.pitch_centipercent == 667);
    assert(audio_engine_stub_pitch_percent_set_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > 6.66f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < 6.68f);
}

static void test_sync_uses_other_deck_effective_bpm(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 100;

    ctrl_event_t deck2_pitch = deck_pitch(CTRL_ID_DECK2_TEMPO, 0);
    deck_core_test_apply_event(&deck2_pitch);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).pitch_centipercent == 1000);

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.pitch_centipercent == -833);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < -8.32f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > -8.34f);
}

static void test_sync_can_exceed_selected_tempo_range_up_to_safe_limit(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 100;
    s_loaded_bpm[CTRL_DECK_2] = 117;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.tempo_range_percent == 10);
    assert(deck1.pitch_centipercent == 1700);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > 16.99f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < 17.01f);
}

static void test_sync_clamps_to_internal_safe_limit(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 100;
    s_loaded_bpm[CTRL_DECK_2] = 130;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.pitch_centipercent == 2000);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] > 19.99f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_1] < 20.01f);
}

static void test_sync_toggle_off_does_not_reapply_pitch(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 128;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);
    assert(audio_engine_stub_pitch_percent_set_count[CTRL_DECK_1] == 1);

    deck_core_test_apply_event(&deck1_sync);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch_centipercent == 667);
    assert(audio_engine_stub_pitch_percent_set_count[CTRL_DECK_1] == 1);
}

static void test_manual_pitch_disables_sync_state(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 128;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    ctrl_event_t deck1_pitch = deck_pitch(CTRL_ID_DECK1_TEMPO, 8192);

    deck_core_test_apply_event(&deck1_sync);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);

    deck_core_test_apply_event(&deck1_pitch);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pitch_centipercent == 0);
}

static anlz_beat_t s_sync_target_beats[] = {
    {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 1500, .beat_phase = 1, .bpm_x100 = 12000},
    {.time_ms = 2000, .beat_phase = 2, .bpm_x100 = 12000},
    {.time_ms = 2500, .beat_phase = 3, .bpm_x100 = 12000},
    {.time_ms = 3000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 3500, .beat_phase = 1, .bpm_x100 = 12000},
};

static anlz_beat_t s_sync_reference_beats[] = {
    {.time_ms = 8000, .beat_phase = 0, .bpm_x100 = 12800},
    {.time_ms = 8469, .beat_phase = 1, .bpm_x100 = 12800},
    {.time_ms = 8938, .beat_phase = 2, .bpm_x100 = 12800},
    {.time_ms = 9407, .beat_phase = 3, .bpm_x100 = 12800},
};

static void test_sync_phase_aligns_to_matching_reference_beat_phase(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t target_meta;
    static anlz_metadata_t reference_meta;
    target_meta = (anlz_metadata_t) {
        .beats = s_sync_target_beats,
        .beat_count = (uint16_t)(sizeof(s_sync_target_beats) / sizeof(s_sync_target_beats[0])),
        .bpm = 120,
    };
    reference_meta = (anlz_metadata_t) {
        .beats = s_sync_reference_beats,
        .beat_count = (uint16_t)(sizeof(s_sync_reference_beats) / sizeof(s_sync_reference_beats[0])),
        .bpm = 128,
    };
    s_loaded_anlz[CTRL_DECK_1] = &target_meta;
    s_loaded_anlz[CTRL_DECK_2] = &reference_meta;
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 128;
    audio_engine_stub_deck_playing[CTRL_DECK_1] = false;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 2600;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 8900;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.pitch_centipercent == 667);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 2000);
    assert(deck1.position_ms == 2000);
}

static void test_sync_skips_phase_align_seek_while_target_deck_is_playing(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t target_meta;
    static anlz_metadata_t reference_meta;
    target_meta = (anlz_metadata_t) {
        .beats = s_sync_target_beats,
        .beat_count = (uint16_t)(sizeof(s_sync_target_beats) / sizeof(s_sync_target_beats[0])),
        .bpm = 120,
    };
    reference_meta = (anlz_metadata_t) {
        .beats = s_sync_reference_beats,
        .beat_count = (uint16_t)(sizeof(s_sync_reference_beats) / sizeof(s_sync_reference_beats[0])),
        .bpm = 128,
    };
    s_loaded_anlz[CTRL_DECK_1] = &target_meta;
    s_loaded_anlz[CTRL_DECK_2] = &reference_meta;
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 128;
    audio_engine_stub_deck_playing[CTRL_DECK_1] = true;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 2600;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 8900;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.pitch_centipercent == 667);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 2600);
}

static void test_sync_without_beatgrid_keeps_phase_position_unchanged(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    s_loaded_bpm[CTRL_DECK_2] = 128;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 2600;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 8900;

    ctrl_event_t deck1_sync = deck_button(CTRL_ID_DECK1_SYNC);
    deck_core_test_apply_event(&deck1_sync);

    deck_state_t deck1 = deck_core_test_get_deck_state(CTRL_DECK_1);
    assert(deck1.sync_enabled);
    assert(deck1.pitch_centipercent == 667);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 2600);
}

static void test_loop_in_out_sets_requested_deck_loop_from_audio_position(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 1000;

    ctrl_event_t loop_in = deck_button(CTRL_ID_DECK2_LOOP_IN);
    ctrl_event_t loop_out = deck_button(CTRL_ID_DECK2_LOOP_OUT);

    deck_core_test_apply_event(&loop_in);
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 2600;
    deck_core_test_apply_event(&loop_out);

    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_active[CTRL_DECK_2]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 1000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 2600);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_2] == 1);
}

static void test_smart_buttons_toggle_audio_state_and_leds(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    control_link_stub_reset_leds();

    ctrl_event_t smart_cfx_press = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_SMART_CFX,
        .value = 1,
    };
    ctrl_event_t smart_cfx_release = smart_cfx_press;
    smart_cfx_release.value = 0;
    ctrl_event_t smart_fader_press = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_SMART_FADER,
        .value = 1,
    };

    deck_core_test_apply_event(&smart_cfx_press);
    assert(audio_engine_stub_smart_cfx_enabled);
    assert(control_link_stub_last_led_state(LED_SMART_CFX, CTRL_DECK_1) == 1);

    deck_core_test_apply_event(&smart_cfx_release);
    assert(audio_engine_stub_smart_cfx_enabled);
    assert(control_link_stub_last_led_state(LED_SMART_CFX, CTRL_DECK_1) == 1);

    deck_core_test_apply_event(&smart_cfx_press);
    assert(!audio_engine_stub_smart_cfx_enabled);
    assert(control_link_stub_last_led_state(LED_SMART_CFX, CTRL_DECK_1) == 0);

    deck_core_test_apply_event(&smart_fader_press);
    assert(audio_engine_stub_smart_fader_enabled);
    assert(control_link_stub_last_led_state(LED_SMART_FADER, CTRL_DECK_1) == 1);
}

static void test_beat_fx_defaults_and_state_controls(void)
{
    deck_core_test_reset();

    deck_core_beat_fx_state_t state = deck_core_test_get_beat_fx_state();
    assert(state.effect == DECK_CORE_BEAT_FX_FILTER);
    assert(state.beat == DECK_CORE_BEAT_FX_BEAT_1);
    assert(state.target == CTRL_BEAT_FX_TARGET_BOTH);
    assert(state.depth == 64);
    assert(!state.enabled);

    ctrl_event_t next = beat_fx_button(CTRL_ID_BEAT_FX_SELECT_NEXT, 1);
    deck_core_test_apply_event(&next);
    state = deck_core_test_get_beat_fx_state();
    assert(state.effect == DECK_CORE_BEAT_FX_ECHO);

    ctrl_event_t prev = beat_fx_button(CTRL_ID_BEAT_FX_SELECT_PREV, 1);
    deck_core_test_apply_event(&prev);
    state = deck_core_test_get_beat_fx_state();
    assert(state.effect == DECK_CORE_BEAT_FX_FILTER);

    ctrl_event_t inc = beat_fx_button(CTRL_ID_BEAT_FX_BEAT_INC, 1);
    deck_core_test_apply_event(&inc);
    state = deck_core_test_get_beat_fx_state();
    assert(state.beat == DECK_CORE_BEAT_FX_BEAT_2);

    ctrl_event_t dec = beat_fx_button(CTRL_ID_BEAT_FX_BEAT_DEC, 1);
    deck_core_test_apply_event(&dec);
    deck_core_test_apply_event(&dec);
    state = deck_core_test_get_beat_fx_state();
    assert(state.beat == DECK_CORE_BEAT_FX_BEAT_1_2);

    ctrl_event_t target = beat_fx_button(CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH2);
    deck_core_test_apply_event(&target);
    state = deck_core_test_get_beat_fx_state();
    assert(state.target == CTRL_BEAT_FX_TARGET_CH2);

    ctrl_event_t depth = beat_fx_depth(127);
    deck_core_test_apply_event(&depth);
    state = deck_core_test_get_beat_fx_state();
    assert(state.depth == 127);
}

static void test_beat_fx_public_snapshot_matches_state_controls(void)
{
    deck_core_test_reset();

    ctrl_event_t target = beat_fx_button(CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    ctrl_event_t depth = beat_fx_depth(42);
    ctrl_event_t on = beat_fx_button(CTRL_ID_BEAT_FX_ON, 1);
    deck_core_test_apply_event(&target);
    deck_core_test_apply_event(&depth);
    deck_core_test_apply_event(&on);

    deck_core_beat_fx_state_t state = deck_core_get_beat_fx_state();
    assert(state.effect == DECK_CORE_BEAT_FX_FILTER);
    assert(state.beat == DECK_CORE_BEAT_FX_BEAT_1);
    assert(state.target == CTRL_BEAT_FX_TARGET_CH1);
    assert(state.depth == 42);
    assert(state.enabled);
}

static void test_beat_fx_on_toggles_on_press_only_and_clear_resets(void)
{
    deck_core_test_reset();
    control_link_stub_reset_leds();

    ctrl_event_t on = beat_fx_button(CTRL_ID_BEAT_FX_ON, 1);
    ctrl_event_t release = beat_fx_button(CTRL_ID_BEAT_FX_ON, 0);
    deck_core_test_apply_event(&on);
    assert(deck_core_test_get_beat_fx_state().enabled);
    assert(control_link_stub_last_led_state(LED_BEAT_FX_ON, CTRL_DECK_1) == 1);
    deck_core_test_apply_event(&release);
    assert(deck_core_test_get_beat_fx_state().enabled);
    assert(control_link_stub_last_led_state(LED_BEAT_FX_ON, CTRL_DECK_1) == 1);
    deck_core_test_apply_event(&on);
    assert(!deck_core_test_get_beat_fx_state().enabled);
    assert(control_link_stub_last_led_state(LED_BEAT_FX_ON, CTRL_DECK_1) == 0);

    ctrl_event_t next = beat_fx_button(CTRL_ID_BEAT_FX_SELECT_NEXT, 1);
    ctrl_event_t depth = beat_fx_depth(12);
    ctrl_event_t target = beat_fx_button(CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    ctrl_event_t clear = beat_fx_button(CTRL_ID_BEAT_FX_CLEAR, 1);
    deck_core_test_apply_event(&next);
    deck_core_test_apply_event(&depth);
    deck_core_test_apply_event(&target);
    deck_core_test_apply_event(&on);
    deck_core_test_apply_event(&clear);

    deck_core_beat_fx_state_t state = deck_core_test_get_beat_fx_state();
    assert(state.effect == DECK_CORE_BEAT_FX_FILTER);
    assert(state.beat == DECK_CORE_BEAT_FX_BEAT_1);
    assert(state.target == CTRL_BEAT_FX_TARGET_BOTH);
    assert(state.depth == 64);
    assert(!state.enabled);
    assert(control_link_stub_last_led_state(LED_BEAT_FX_ON, CTRL_DECK_1) == 0);
}

static void test_beat_fx_filter_state_updates_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t target = beat_fx_button(CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH2);
    ctrl_event_t depth = beat_fx_depth(127);
    ctrl_event_t on = beat_fx_button(CTRL_ID_BEAT_FX_ON, 1);
    deck_core_test_apply_event(&target);
    deck_core_test_apply_event(&depth);
    deck_core_test_apply_event(&on);

    assert(audio_engine_stub_beat_fx_filter_set_count > 0);
    assert(audio_engine_stub_beat_fx_filter_target == AUDIO_ENGINE_BEAT_FX_TARGET_CH2);
    assert(audio_engine_stub_beat_fx_filter_depth == 127);
    assert(audio_engine_stub_beat_fx_filter_enabled);

    ctrl_event_t clear = beat_fx_button(CTRL_ID_BEAT_FX_CLEAR, 1);
    deck_core_test_apply_event(&clear);
    assert(audio_engine_stub_beat_fx_filter_target == AUDIO_ENGINE_BEAT_FX_TARGET_BOTH);
    assert(audio_engine_stub_beat_fx_filter_depth == 64);
    assert(!audio_engine_stub_beat_fx_filter_enabled);
}

static void test_beat_fx_echo_state_updates_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t next = beat_fx_button(CTRL_ID_BEAT_FX_SELECT_NEXT, 1);
    ctrl_event_t target = beat_fx_button(CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    ctrl_event_t beat_inc = beat_fx_button(CTRL_ID_BEAT_FX_BEAT_INC, 1);
    ctrl_event_t depth = beat_fx_depth(96);
    ctrl_event_t on = beat_fx_button(CTRL_ID_BEAT_FX_ON, 1);

    deck_core_test_apply_event(&next);
    deck_core_test_apply_event(&target);
    deck_core_test_apply_event(&beat_inc);
    deck_core_test_apply_event(&depth);
    deck_core_test_apply_event(&on);

    assert(audio_engine_stub_beat_fx_echo_set_count > 0);
    assert(audio_engine_stub_beat_fx_echo_target == AUDIO_ENGINE_BEAT_FX_TARGET_CH1);
    assert(audio_engine_stub_beat_fx_echo_depth == 96);
    assert(audio_engine_stub_beat_fx_echo_delay_ms == 1000);
    assert(audio_engine_stub_beat_fx_echo_enabled);
    assert(!audio_engine_stub_beat_fx_filter_enabled);

    ctrl_event_t clear = beat_fx_button(CTRL_ID_BEAT_FX_CLEAR, 1);
    deck_core_test_apply_event(&clear);
    assert(!audio_engine_stub_beat_fx_echo_enabled);
}

static void test_duplicate_flx4_connected_state_does_not_resend_forced_snapshot(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    control_link_stub_reset_leds();

    ctrl_event_t connected = flx4_connection_state(CTRL_FLX4_CONNECTED);

    deck_core_test_apply_event(&connected);
    int first_snapshot_led_count = control_link_stub_led_count;
    assert(first_snapshot_led_count > 0);

    deck_core_test_apply_event(&connected);
    assert(control_link_stub_led_count == first_snapshot_led_count);
}

static void test_loop_in_marker_publishes_loop_in_led_before_loop_out(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    control_link_stub_reset_leds();
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 1000;

    ctrl_event_t loop_in = deck_button(CTRL_ID_DECK2_LOOP_IN);
    deck_core_test_apply_event(&loop_in);

    assert(control_link_stub_led_count > 0);
    assert(!audio_engine_stub_loop_active[CTRL_DECK_2]);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_2] == 0);
    assert(control_link_stub_last_led_state(LED_LOOP_IN, CTRL_DECK_2) == 1);
    assert(control_link_stub_last_led_state(LED_LOOP_OUT, CTRL_DECK_2) == 0);
}

static void test_reloop_exit_clears_and_restores_last_requested_deck_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 500;

    ctrl_event_t loop_in = deck_button(CTRL_ID_DECK1_LOOP_IN);
    ctrl_event_t loop_out = deck_button(CTRL_ID_DECK1_LOOP_OUT);
    ctrl_event_t reloop_exit = deck_button(CTRL_ID_DECK1_RELOOP_EXIT);

    deck_core_test_apply_event(&loop_in);
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 2500;
    deck_core_test_apply_event(&loop_out);
    deck_core_test_apply_event(&reloop_exit);

    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_clear_count[CTRL_DECK_1] == 1);

    deck_core_test_apply_event(&reloop_exit);

    assert(audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 500);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 2500);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_1] == 2);
}

static void test_loop_halve_and_double_resize_active_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_loop_active[CTRL_DECK_2] = true;
    audio_engine_stub_loop_start_ms[CTRL_DECK_2] = 1000;
    audio_engine_stub_loop_end_ms[CTRL_DECK_2] = 5000;

    ctrl_event_t halve = deck_button(CTRL_ID_DECK2_LOOP_HALVE);
    ctrl_event_t double_loop = deck_button(CTRL_ID_DECK2_LOOP_DOUBLE);

    deck_core_test_apply_event(&halve);

    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 1000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 3000);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_2] == 1);

    deck_core_test_apply_event(&double_loop);

    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 1000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 5000);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_2] == 2);
}

static void test_beat_loop_pad_sets_loop_on_requested_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_2] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 10000;

    ctrl_event_t pad6 = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    pad6.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 5, false, true);
    deck_core_test_apply_event(&pad6);

    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_active[CTRL_DECK_2]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 10000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 10500);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_2] == 1);
}

static void test_beat_loop_pad_maps_pad_index_to_loop_length(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t pad5 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad5.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 4, false, true);
    deck_core_test_apply_event(&pad5);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 20000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 20250);

    ctrl_event_t pad8 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad8.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 7, false, true);
    deck_core_test_apply_event(&pad8);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 20000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 22000);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_1] == 2);
}

static void test_beat_loop_release_event_does_not_set_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 5, false, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_loop_set_count[CTRL_DECK_1] == 0);
    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
}

static void test_shifted_beat_loop_release_restores_previous_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_2] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 10000;
    audio_engine_stub_loop_active[CTRL_DECK_2] = true;
    audio_engine_stub_loop_start_ms[CTRL_DECK_2] = 2000;
    audio_engine_stub_loop_end_ms[CTRL_DECK_2] = 4000;

    ctrl_event_t press = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    press.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 5, true, true);
    deck_core_test_apply_event(&press);

    assert(audio_engine_stub_loop_active[CTRL_DECK_2]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 10000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 10500);

    ctrl_event_t release = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 5, true, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_loop_active[CTRL_DECK_2]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 2000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 4000);
}

static void test_shifted_beat_loop_release_clears_when_no_previous_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 30000;

    ctrl_event_t press = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    press.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 7, true, true);
    deck_core_test_apply_event(&press);

    assert(audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 30000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 32000);

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 7, true, false);
    deck_core_test_apply_event(&release);

    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_clear_count[CTRL_DECK_1] == 1);
}

static void test_pad_mode_buttons_update_requested_deck_mode(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck1_mode = deck_button(CTRL_ID_DECK1_PAD_MODE_BEAT_LOOP);
    ctrl_event_t deck2_mode = deck_button(CTRL_ID_DECK2_PAD_MODE_KEY_SHIFT);
    deck_core_test_apply_event(&deck1_mode);
    deck_core_test_apply_event(&deck2_mode);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).perf_mode == PERF_MODE_BEAT_LOOP);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).perf_mode == PERF_MODE_KEY_SHIFT);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pad_mode == CTRL_PAD_MODE_BEAT_LOOP);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).pad_mode == CTRL_PAD_MODE_KEY_SHIFT);
}

static void test_deferred_pad_mode_buttons_are_consumed_without_transport_side_effects(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t deck1_pad_fx = deck_button(CTRL_ID_DECK1_PAD_MODE_PAD_FX1);
    ctrl_event_t deck2_sampler = deck_button(CTRL_ID_DECK2_PAD_MODE_SAMPLER);

    deck_core_test_apply_event(&deck1_pad_fx);
    deck_core_test_apply_event(&deck2_sampler);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).perf_mode == PERF_MODE_HOT_CUE);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).perf_mode == PERF_MODE_HOT_CUE);
    assert(deck_core_test_get_deck_state(CTRL_DECK_1).pad_mode == CTRL_PAD_MODE_PAD_FX1);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).pad_mode == CTRL_PAD_MODE_SAMPLER);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).playing);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);
    assert(!audio_engine_stub_deck_playing[CTRL_DECK_1]);
    assert(!audio_engine_stub_deck_playing[CTRL_DECK_2]);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 0);
}

static void test_pad_action_is_consumed_without_transport_side_effects(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t pad = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    pad.value = CTRL_PAD_ACTION_VALUE(PERF_MODE_HOT_CUE, 2, true, true);

    deck_core_test_apply_event(&pad);

    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 0);

    pad.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_SAMPLER, 4, false, true);
    assert(CTRL_PAD_ACTION_MODE(pad.value) == CTRL_PAD_MODE_SAMPLER);
    assert(CTRL_PAD_ACTION_PAD(pad.value) == 4);
    assert(CTRL_PAD_ACTION_PRESSED(pad.value));
    assert(!CTRL_PAD_ACTION_SHIFTED(pad.value));
}

static void test_pad_fx1_pad_action_routes_to_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t mode = deck_button(CTRL_ID_DECK1_PAD_MODE_PAD_FX1);
    deck_core_test_apply_event(&mode);

    ctrl_event_t press = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    press.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, true);
    deck_core_test_apply_event(&press);

    assert(audio_engine_stub_pad_fx_set_count == 1);
    assert(audio_engine_stub_pad_fx_deck == CTRL_DECK_1);
    assert(audio_engine_stub_pad_fx_mode == AUDIO_PAD_FX_MODE_PAD_FX1);
    assert(audio_engine_stub_pad_fx_pad == 2);
    assert(audio_engine_stub_pad_fx_active);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).playing);

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_pad_fx_set_count == 2);
    assert(audio_engine_stub_pad_fx_deck == CTRL_DECK_1);
    assert(audio_engine_stub_pad_fx_mode == AUDIO_PAD_FX_MODE_PAD_FX1);
    assert(audio_engine_stub_pad_fx_pad == 2);
    assert(!audio_engine_stub_pad_fx_active);
}

static void test_pad_fx2_pad_action_routes_to_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t mode = deck_button(CTRL_ID_DECK2_PAD_MODE_PAD_FX2);
    deck_core_test_apply_event(&mode);

    ctrl_event_t press = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    press.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 3, true, true);
    deck_core_test_apply_event(&press);

    assert(audio_engine_stub_pad_fx_set_count == 1);
    assert(audio_engine_stub_pad_fx_deck == CTRL_DECK_2);
    assert(audio_engine_stub_pad_fx_mode == AUDIO_PAD_FX_MODE_PAD_FX2);
    assert(audio_engine_stub_pad_fx_pad == 3);
    assert(audio_engine_stub_pad_fx_active);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).playing);
}

static void test_pad_fx_pad_action_updates_momentary_pad_led(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    control_link_stub_reset_leds();

    ctrl_event_t mode = deck_button(CTRL_ID_DECK1_PAD_MODE_PAD_FX1);
    deck_core_test_apply_event(&mode);
    control_link_stub_reset_leds();

    ctrl_event_t press = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    press.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, true);
    deck_core_test_apply_event(&press);
    assert(control_link_stub_last_led_state(LED_PAD_FX1_PAD_3, CTRL_DECK_1) == 1);

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, false);
    deck_core_test_apply_event(&release);
    assert(control_link_stub_last_led_state(LED_PAD_FX1_PAD_3, CTRL_DECK_1) == 0);
}

static void test_hot_cue_pad_stores_empty_slot_at_requested_deck_position(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    clear_test_hot_cues();
    s_loaded_track_key[CTRL_DECK_1] = 1001;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 12345;

    ctrl_event_t pad = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 2, false, true);

    deck_core_test_apply_event(&pad);

    hot_cue_store_blob_t blob = {0};
    assert(hot_cue_store_load(1001, &blob) == ESP_OK);
    assert((blob.valid_mask & (1u << 2)) != 0);
    assert(blob.slots[2].pos_ms == 12345);
    assert(blob.slots[2].end_ms == 0);
    assert(blob.slots[2].type == HOT_CUE_STORE_TYPE_SINGLE);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
}

static void test_hot_cue_pad_recalls_existing_slot_on_requested_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    clear_test_hot_cues();
    s_loaded_track_key[CTRL_DECK_2] = 2002;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 30000;

    hot_cue_store_blob_t blob = {0};
    blob.valid_mask = (1u << 4);
    blob.slots[4].pos_ms = 5555;
    blob.slots[4].type = HOT_CUE_STORE_TYPE_SINGLE;
    assert(hot_cue_store_save(2002, &blob) == ESP_OK);

    ctrl_event_t pad = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    pad.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 4, false, true);

    deck_core_test_apply_event(&pad);

    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 5555);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).position_ms == 5555);
}

static void test_shift_hot_cue_pad_clears_requested_slot(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    clear_test_hot_cues();
    s_loaded_track_key[CTRL_DECK_1] = 3003;

    hot_cue_store_blob_t blob = {0};
    blob.valid_mask = (1u << 1) | (1u << 6);
    blob.slots[1].pos_ms = 1111;
    blob.slots[1].type = HOT_CUE_STORE_TYPE_SINGLE;
    blob.slots[6].pos_ms = 6666;
    blob.slots[6].type = HOT_CUE_STORE_TYPE_SINGLE;
    assert(hot_cue_store_save(3003, &blob) == ESP_OK);

    ctrl_event_t pad = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 1, true, true);

    deck_core_test_apply_event(&pad);

    hot_cue_store_blob_t loaded = {0};
    assert(hot_cue_store_load(3003, &loaded) == ESP_OK);
    assert((loaded.valid_mask & (1u << 1)) == 0);
    assert((loaded.valid_mask & (1u << 6)) != 0);
    assert(loaded.slots[1].pos_ms == 0);
    assert(loaded.slots[1].type == 0);
    assert(loaded.slots[6].pos_ms == 6666);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
}

static anlz_beat_t s_beat_jump_beats[] = {
    {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 2000, .beat_phase = 1, .bpm_x100 = 12000},
    {.time_ms = 3000, .beat_phase = 2, .bpm_x100 = 12000},
    {.time_ms = 4000, .beat_phase = 3, .bpm_x100 = 12000},
    {.time_ms = 5000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 6000, .beat_phase = 1, .bpm_x100 = 12000},
    {.time_ms = 7000, .beat_phase = 2, .bpm_x100 = 12000},
    {.time_ms = 8000, .beat_phase = 3, .bpm_x100 = 12000},
    {.time_ms = 9000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 10000, .beat_phase = 1, .bpm_x100 = 12000},
};

static anlz_metadata_t beat_jump_meta(void)
{
    return (anlz_metadata_t) {
        .beats = s_beat_jump_beats,
        .beat_count = (uint16_t)(sizeof(s_beat_jump_beats) / sizeof(s_beat_jump_beats[0])),
        .bpm = 120,
    };
}

static void test_beat_jump_buttons_seek_by_one_beat_on_requested_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t meta;
    meta = beat_jump_meta();
    s_loaded_anlz[CTRL_DECK_2] = &meta;
    s_loaded_bpm[CTRL_DECK_2] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 4200;
    audio_engine_stub_deck_playing[CTRL_DECK_2] = true;

    ctrl_event_t back = deck_button(CTRL_ID_DECK2_BEAT_JUMP_BACK);
    ctrl_event_t forward = deck_button(CTRL_ID_DECK2_BEAT_JUMP_FORWARD);

    deck_core_test_apply_event(&back);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 3000);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).playing);

    deck_core_test_apply_event(&forward);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 2);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 4000);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).playing);
}

static void test_beat_jump_pad_maps_pad_index_to_jump_size(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t pad4 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad4.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 3, false, true);
    deck_core_test_apply_event(&pad4);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 18000);

    ctrl_event_t pad5 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad5.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 4, false, true);
    deck_core_test_apply_event(&pad5);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 2);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 20000);
}

static void test_beat_jump_release_event_does_not_seek(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 4, false, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 20000);
}

static void test_beat_jump_clamps_to_beatgrid_edges(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t meta;
    meta = beat_jump_meta();
    s_loaded_anlz[CTRL_DECK_1] = &meta;
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 1200;

    ctrl_event_t pad1 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad1.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 0, false, true);
    deck_core_test_apply_event(&pad1);

    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 1000);
}

static void test_smoke_log_policy_rates_limits_deferred_analog_controls(void)
{
    deck_core_test_reset();

    assert(deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 0));
    assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 100));
    assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 512));
    assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 1024));
    assert(deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 2048));
    assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 3000));
    assert(deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 4096));

    assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_VOLUME, 2048));
}

static void test_smoke_log_policy_logs_deferred_buttons_only_on_press(void)
{
    assert(deck_core_test_should_log_deferred_button(CTRL_ID_DECK1_SYNC, 1));
    assert(!deck_core_test_should_log_deferred_button(CTRL_ID_DECK1_SYNC, 0));
    assert(deck_core_test_should_log_deferred_button(CTRL_ID_DECK2_PAD_ACTION,
                                                     CTRL_PAD_ACTION_VALUE(PERF_MODE_HOT_CUE, 3, false, true)));
    assert(!deck_core_test_should_log_deferred_button(CTRL_ID_DECK2_PAD_ACTION,
                                                      CTRL_PAD_ACTION_VALUE(PERF_MODE_HOT_CUE, 3, false, false)));
}

int main(void)
{
    test_decks_track_transport_independently();
    test_deck2_snapshot_follows_audio_engine_position();
    test_failed_deck_play_does_not_mark_deck_playing();
    test_decks_track_pitch_independently();
    test_tempo_range_defaults_to_ten_percent();
    test_tempo_range_button_cycles_requested_deck_only();
    test_tempo_range_release_does_not_cycle();
    test_pitch_mapping_uses_selected_tempo_range();
    test_tempo_range_change_reapplies_current_pitch();
    test_cue_shift_jumps_requested_deck_to_track_start();
    test_browser_namespace_routes_load_to_requested_deck();
    test_browser_namespace_routes_browse_delta();
    test_browser_press_toggles_library_view_without_loading_deck();
    test_mixer_namespace_routes_volume_and_crossfader();
    test_mixer_namespace_routes_eq_controls();
    test_mixer_namespace_routes_filter_controls();
    test_mixer_namespace_routes_pfl_toggle_on_press();
    test_smart_buttons_toggle_audio_state_and_leds();
    test_beat_fx_defaults_and_state_controls();
    test_beat_fx_public_snapshot_matches_state_controls();
    test_beat_fx_on_toggles_on_press_only_and_clear_resets();
    test_beat_fx_filter_state_updates_audio_engine();
    test_beat_fx_echo_state_updates_audio_engine();
    test_duplicate_flx4_connected_state_does_not_resend_forced_snapshot();
    test_sync_button_toggles_requested_deck_sync_led_state();
    test_sync_matches_requested_deck_to_other_deck_bpm();
    test_sync_uses_other_deck_effective_bpm();
    test_sync_can_exceed_selected_tempo_range_up_to_safe_limit();
    test_sync_clamps_to_internal_safe_limit();
    test_sync_toggle_off_does_not_reapply_pitch();
    test_manual_pitch_disables_sync_state();
    test_sync_phase_aligns_to_matching_reference_beat_phase();
    test_sync_skips_phase_align_seek_while_target_deck_is_playing();
    test_sync_without_beatgrid_keeps_phase_position_unchanged();
    test_loop_in_out_sets_requested_deck_loop_from_audio_position();
    test_loop_in_marker_publishes_loop_in_led_before_loop_out();
    test_reloop_exit_clears_and_restores_last_requested_deck_loop();
    test_loop_halve_and_double_resize_active_loop();
    test_beat_loop_pad_sets_loop_on_requested_deck();
    test_beat_loop_pad_maps_pad_index_to_loop_length();
    test_beat_loop_release_event_does_not_set_loop();
    test_shifted_beat_loop_release_restores_previous_loop();
    test_shifted_beat_loop_release_clears_when_no_previous_loop();
    test_pad_mode_buttons_update_requested_deck_mode();
    test_deferred_pad_mode_buttons_are_consumed_without_transport_side_effects();
    test_pad_action_is_consumed_without_transport_side_effects();
    test_pad_fx1_pad_action_routes_to_audio_engine();
    test_pad_fx2_pad_action_routes_to_audio_engine();
    test_pad_fx_pad_action_updates_momentary_pad_led();
    test_hot_cue_pad_stores_empty_slot_at_requested_deck_position();
    test_hot_cue_pad_recalls_existing_slot_on_requested_deck();
    test_shift_hot_cue_pad_clears_requested_slot();
    test_beat_jump_buttons_seek_by_one_beat_on_requested_deck();
    test_beat_jump_pad_maps_pad_index_to_jump_size();
    test_beat_jump_release_event_does_not_seek();
    test_beat_jump_clamps_to_beatgrid_edges();
    test_smoke_log_policy_rates_limits_deferred_analog_controls();
    test_smoke_log_policy_logs_deferred_buttons_only_on_press();
    puts("deck_core_dual tests passed");
    return 0;
}
