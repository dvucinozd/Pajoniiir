#include "flx4_map.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>

#define MSG(status_, data1_, data2_) (&(flx4_midi_message_t) { \
    .cable = 0, \
    .cin = (((status_) & 0xF0) == 0xB0) ? FLX4_USB_MIDI_CIN_CONTROL_CHANGE : FLX4_USB_MIDI_CIN_NOTE_ON, \
    .len = 3, \
    .status = (status_), \
    .data1 = (data1_), \
    .data2 = (data2_), \
})

static void expect_event(const flx4_control_event_t *ev, uint8_t type, uint8_t id, int16_t value)
{
    assert(ev->type == type);
    assert(ev->id == id);
    assert(ev->value == value);
}

static void test_transport_load_and_pfl_buttons(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x90, 0x0B, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, 1);
    assert(flx4_map_message(&state, MSG(0x90, 0x0B, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, 0);

    assert(flx4_map_message(&state, MSG(0x91, 0x0C, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_CUE, 1);

    assert(flx4_map_message(&state, MSG(0x96, 0x46, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_LOAD_DECK1, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x47, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_LOAD_DECK2, 1);

    assert(flx4_map_message(&state, MSG(0x90, 0x54, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PFL, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x54, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PFL, 1);
}

static void test_jog_and_browse_relative_controls(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0xB0, 0x22, 65), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_SCRATCH, 1);
    assert(flx4_map_message(&state, MSG(0xB1, 0x23, 63), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_DECK2_JOG_BEND, -1);
    assert(flx4_map_message(&state, MSG(0xB0, 0x21, 66), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_BEND, 2);
    assert(flx4_map_message(&state, MSG(0x90, 0x36, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_TOUCH, 1);

    assert(flx4_map_message(&state, MSG(0xB6, 0x40, 62), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_DELTA, -2);
    assert(!flx4_map_message(&state, MSG(0xB6, 0x40, 64), &ev));
}

static void test_14bit_controls_emit_after_both_halves(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(!flx4_map_message(&state, MSG(0xB0, 0x00, 0x12), &ev));
    assert(flx4_map_message(&state, MSG(0xB0, 0x20, 0x34), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_DECK1_TEMPO, (int16_t)((0x12 << 7) | 0x34));

    assert(!flx4_map_message(&state, MSG(0xB1, 0x33, 0x01), &ev));
    assert(flx4_map_message(&state, MSG(0xB1, 0x13, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH2_VOLUME, (int16_t)((0x7F << 7) | 0x01));

    assert(!flx4_map_message(&state, MSG(0xB6, 0x1F, 0x20), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x3F, 0x02), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CROSSFADER, (int16_t)((0x20 << 7) | 0x02));
}

static void test_unsupported_messages_are_ignored(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(!flx4_map_message(NULL, MSG(0x90, 0x0B, 0x7F), &ev));
    assert(!flx4_map_message(&state, NULL, &ev));
    assert(!flx4_map_message(&state, MSG(0x96, 0x41, 0x7F), &ev));
    assert(!flx4_map_message(&state, MSG(0x90, 0x58, 0x7F), &ev));
}

int main(void)
{
    test_transport_load_and_pfl_buttons();
    test_jog_and_browse_relative_controls();
    test_14bit_controls_emit_after_both_halves();
    test_unsupported_messages_are_ignored();
    puts("flx4_map tests passed");
    return 0;
}
