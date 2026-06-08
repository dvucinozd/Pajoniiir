#include "deck_core.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>

static ctrl_event_t deck_button(uint8_t id)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BUTTON,
        .id = id,
        .value = 1,
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

int main(void)
{
    test_decks_track_transport_independently();
    test_decks_track_pitch_independently();
    puts("deck_core_dual tests passed");
    return 0;
}
