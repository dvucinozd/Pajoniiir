#include "deck_core.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>

static int s_load_calls[DECK_CORE_DECK_COUNT];
static int s_browse_delta;
static int s_toggle_library_view_calls;
int audio_engine_stub_channel_volume[DECK_CORE_DECK_COUNT];
int audio_engine_stub_crossfader;
int audio_engine_stub_pfl_toggle_count[DECK_CORE_DECK_COUNT];
esp_err_t audio_engine_stub_deck_play_result[DECK_CORE_DECK_COUNT];
bool audio_engine_stub_deck_playing[DECK_CORE_DECK_COUNT];
uint32_t audio_engine_stub_deck_position_ms[DECK_CORE_DECK_COUNT];
int audio_engine_stub_deck_seek_count[DECK_CORE_DECK_COUNT];
bool audio_engine_stub_loop_active[DECK_CORE_DECK_COUNT];

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

static void reset_audio_engine_stub(void)
{
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        audio_engine_stub_deck_play_result[deck] = ESP_OK;
        audio_engine_stub_deck_playing[deck] = false;
        audio_engine_stub_deck_position_ms[deck] = 0;
        audio_engine_stub_deck_seek_count[deck] = 0;
        audio_engine_stub_loop_active[deck] = false;
    }
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
    test_cue_shift_jumps_requested_deck_to_track_start();
    test_browser_namespace_routes_load_to_requested_deck();
    test_browser_namespace_routes_browse_delta();
    test_browser_press_toggles_library_view_without_loading_deck();
    test_mixer_namespace_routes_volume_and_crossfader();
    test_mixer_namespace_routes_pfl_toggle_on_press();
    test_sync_button_toggles_requested_deck_sync_led_state();
    test_pad_mode_buttons_update_requested_deck_mode();
    test_deferred_pad_mode_buttons_are_consumed_without_transport_side_effects();
    test_pad_action_is_consumed_without_transport_side_effects();
    test_smoke_log_policy_rates_limits_deferred_analog_controls();
    test_smoke_log_policy_logs_deferred_buttons_only_on_press();
    puts("deck_core_dual tests passed");
    return 0;
}
