#include "deck_core.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>

static int s_load_calls[DECK_CORE_DECK_COUNT];
static int s_browse_delta;

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

static void test_decks_track_transport_independently(void)
{
    deck_core_test_reset();

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

static void test_decks_track_pitch_independently(void)
{
    deck_core_test_reset();

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
    s_browse_delta = 0;

    ctrl_event_t browse = browse_delta(3);
    deck_core_test_apply_event(&browse);

    assert(s_browse_delta == 3);
}

int main(void)
{
    test_decks_track_transport_independently();
    test_decks_track_pitch_independently();
    test_browser_namespace_routes_load_to_requested_deck();
    test_browser_namespace_routes_browse_delta();
    puts("deck_core_dual tests passed");
    return 0;
}
