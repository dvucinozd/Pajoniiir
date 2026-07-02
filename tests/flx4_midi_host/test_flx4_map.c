#include "flx4_map.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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

typedef struct {
    flx4_control_event_t events[32];
    size_t count;
} snapshot_capture_t;

static bool capture_snapshot_event(uint8_t type, uint8_t id, int16_t value, void *ctx)
{
    snapshot_capture_t *capture = (snapshot_capture_t *)ctx;
    assert(capture->count < (sizeof(capture->events) / sizeof(capture->events[0])));
    capture->events[capture->count].type = type;
    capture->events[capture->count].id = id;
    capture->events[capture->count].value = value;
    capture->count++;
    return true;
}

static int find_snapshot_event(const snapshot_capture_t *capture, uint8_t id)
{
    for (size_t i = 0; i < capture->count; i++) {
        if (capture->events[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static void expect_snapshot_event(const snapshot_capture_t *capture,
                                  uint8_t id,
                                  int16_t value)
{
    int idx = find_snapshot_event(capture, id);
    assert(idx >= 0);
    expect_event(&capture->events[idx], CTRL_TYPE_PITCH, id, value);
}

static void expect_no_snapshot_event(const snapshot_capture_t *capture, uint8_t id)
{
    assert(find_snapshot_event(capture, id) < 0);
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
    assert(flx4_map_message(&state, MSG(0x96, 0x46, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_LOAD_DECK1, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x47, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_LOAD_DECK2, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x47, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_LOAD_DECK2, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x68, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK1, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x68, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK1, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x7A, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK2, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x7A, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK2, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x41, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BROWSE_PRESS, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x41, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BROWSE_PRESS, 0);

    assert(flx4_map_message(&state, MSG(0x90, 0x54, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PFL, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x54, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PFL, 1);
}

static void test_smart_control_buttons(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x96, 0x00, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_CFX, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x00, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_CFX, 0);

    assert(flx4_map_message(&state, MSG(0x96, 0x01, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_FADER, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x01, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_FADER, 0);
}

static void test_beat_fx_controls(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x94, 0x63, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_SELECT_NEXT, 1);
    assert(flx4_map_message(&state, MSG(0x94, 0x63, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_SELECT_NEXT, 0);
    assert(flx4_map_message(&state, MSG(0x94, 0x64, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_SELECT_PREV, 1);

    assert(flx4_map_message(&state, MSG(0x94, 0x4A, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_BEAT_DEC, 1);
    assert(flx4_map_message(&state, MSG(0x94, 0x4B, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_BEAT_INC, 1);

    assert(flx4_map_message(&state, MSG(0x94, 0x10, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    assert(!flx4_map_message(&state, MSG(0x94, 0x10, 0x00), &ev));
    assert(flx4_map_message(&state, MSG(0x95, 0x11, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH2);
    assert(flx4_map_message(&state, MSG(0x94, 0x10, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_BOTH);
    assert(flx4_map_message(&state, MSG(0x94, 0x10, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH2);

    flx4_map_init(&state);
    assert(flx4_map_message(&state, MSG(0x94, 0x10, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    assert(flx4_map_message(&state, MSG(0x95, 0x11, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_BOTH);
    assert(flx4_map_message(&state, MSG(0x95, 0x11, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    assert(!flx4_map_message(&state, MSG(0x94, 0x10, 0x00), &ev));

    assert(flx4_map_message(&state, MSG(0xB4, 0x02, 0x40), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_BEAT_FX_DEPTH, 64);

    assert(flx4_map_message(&state, MSG(0x94, 0x47, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_ON, 1);
    assert(flx4_map_message(&state, MSG(0x95, 0x47, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_ON, 0);
    assert(flx4_map_message(&state, MSG(0x94, 0x43, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_CLEAR, 1);
}

static void test_deck_transport_extension_buttons(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x90, 0x3F, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_SHIFT, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x3F, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_SHIFT, 0);

    assert(flx4_map_message(&state, MSG(0x90, 0x48, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_TO_START, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x48, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_TO_START, 1);

    assert(flx4_map_message(&state, MSG(0x90, 0x58, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_SYNC, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x58, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_SYNC, 1);

    assert(flx4_map_message(&state, MSG(0x90, 0x60, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_TEMPO_RANGE, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x60, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_TEMPO_RANGE, 1);
}

static void test_shifted_extended_deck_actions(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x90, 0x0E, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, true));
    assert(flx4_map_message(&state, MSG(0x91, 0x0E, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, false));

    assert(flx4_map_message(&state, MSG(0x90, 0x5C, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_SYNC_MASTER, true));
    assert(flx4_map_message(&state, MSG(0x91, 0x5C, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_SYNC_MASTER, false));

    assert(flx4_map_message(&state, MSG(0x90, 0x50, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_RELOOP_STOP, true));
    assert(flx4_map_message(&state, MSG(0x91, 0x4C, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, true));
    assert(flx4_map_message(&state, MSG(0x90, 0x4E, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT, true));
    assert(flx4_map_message(&state, MSG(0x91, 0x68, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_EXT_ACTION,
                 CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, true));
}

static void test_loop_and_beat_jump_buttons(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x90, 0x10, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_LOOP_IN, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x11, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_LOOP_OUT, 1);
    assert(flx4_map_message(&state, MSG(0x90, 0x4D, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_RELOOP_EXIT, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x51, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_LOOP_HALVE, 1);
    assert(flx4_map_message(&state, MSG(0x90, 0x53, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_LOOP_DOUBLE, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x3E, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_BEAT_JUMP_BACK, 1);
    assert(flx4_map_message(&state, MSG(0x90, 0x3D, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_BEAT_JUMP_FORWARD, 1);
}

static void test_pad_modes_and_pad_actions(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0x90, 0x1B, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_MODE_HOT_CUE, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x1E, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_MODE_PAD_FX1, 1);
    assert(flx4_map_message(&state, MSG(0x90, 0x22, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_MODE_SAMPLER, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x6D, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_MODE_BEAT_LOOP, 1);
    assert(flx4_map_message(&state, MSG(0x90, 0x20, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_MODE_BEAT_JUMP, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x6F, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_MODE_KEY_SHIFT, 1);

    assert(flx4_map_message(&state, MSG(0x97, 0x03, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_HOT_CUE, 3, false, true));
    assert(flx4_map_message(&state, MSG(0x9A, 0x75, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_KEY_SHIFT, 5, true, false));
    assert(flx4_map_message(&state, MSG(0x99, 0x62, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 2, false, true));
    assert(flx4_map_message(&state, MSG(0x98, 0x27, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 7, true, true));
    assert(flx4_map_message(&state, MSG(0x99, 0x34, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_SAMPLER, 4, false, true));
    assert(flx4_map_message(&state, MSG(0x98, 0x45, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_KEYBOARD, 5, true, true));

    assert(flx4_map_message(&state, MSG(0x97, 0x10, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 0, false, true));
    assert(flx4_map_message(&state, MSG(0x97, 0x57, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 7, false, true));
    assert(flx4_map_message(&state, MSG(0x99, 0x12, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, true));
    assert(flx4_map_message(&state, MSG(0x99, 0x50, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_ACTION,
                 CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 0, false, false));
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

    assert(flx4_map_message(&state, MSG(0xB6, 0x40, 0x01), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_DELTA, 1);
    assert(flx4_map_message(&state, MSG(0xB6, 0x40, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_DELTA, -1);
    assert(!flx4_map_message(&state, MSG(0xB6, 0x40, 0x00), &ev));

    assert(flx4_map_message(&state, MSG(0xB6, 0x64, 0x01), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_SHIFT_DELTA, 1);
    assert(flx4_map_message(&state, MSG(0xB6, 0x64, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_SHIFT_DELTA, -1);
    assert(!flx4_map_message(&state, MSG(0xB6, 0x64, 0x00), &ev));
}

static void test_jog_search_and_master_cue_controls(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0xB0, 0x29, 0x41), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_SEARCH, 1);
    assert(flx4_map_message(&state, MSG(0xB1, 0x29, 0x3F), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_DECK2_JOG_SEARCH, -1);
    assert(!flx4_map_message(&state, MSG(0xB0, 0x29, 0x40), &ev));

    assert(flx4_map_message(&state, MSG(0x90, 0x67, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_SEARCH_TOUCH, 1);
    assert(flx4_map_message(&state, MSG(0x91, 0x67, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_JOG_SEARCH_TOUCH, 0);

    assert(flx4_map_message(&state, MSG(0x96, 0x63, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x63, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x78, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x78, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 0);

    assert(flx4_map_message(&state, MSG(0x96, 0x42, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BROWSE_SHIFT_PRESS, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x42, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BROWSE_SHIFT_PRESS, 0);
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

    assert(!flx4_map_message(&state, MSG(0xB6, 0x08, 0x21), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x28, 0x43), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_MASTER_VOLUME, (int16_t)((0x21 << 7) | 0x43));

    assert(!flx4_map_message(&state, MSG(0xB0, 0x04, 0x10), &ev));
    assert(flx4_map_message(&state, MSG(0xB0, 0x24, 0x20), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH1_TRIM, (int16_t)((0x10 << 7) | 0x20));

    assert(!flx4_map_message(&state, MSG(0xB1, 0x07, 0x11), &ev));
    assert(flx4_map_message(&state, MSG(0xB1, 0x27, 0x21), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH2_EQ_HIGH, (int16_t)((0x11 << 7) | 0x21));

    assert(!flx4_map_message(&state, MSG(0xB6, 0x17, 0x12), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x37, 0x22), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH1_FILTER, (int16_t)((0x12 << 7) | 0x22));

    assert(!flx4_map_message(&state, MSG(0xB6, 0x0C, 0x13), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x2C, 0x23), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_HEADPHONE_MIX, (int16_t)((0x13 << 7) | 0x23));

    assert(!flx4_map_message(&state, MSG(0xB6, 0x0D, 0x14), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x2D, 0x24), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_HEADPHONE_LEVEL, (int16_t)((0x14 << 7) | 0x24));
}

static void test_snapshot_emits_known_absolute_controls_only(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    snapshot_capture_t capture;
    flx4_map_init(&state);
    memset(&capture, 0, sizeof(capture));

    assert(!flx4_map_message(&state, MSG(0xB0, 0x04, 0x10), &ev));
    assert(flx4_map_emit_snapshot(&state, capture_snapshot_event, &capture) == 0);
    assert(!flx4_map_message(&state, MSG(0xB6, 0x0D, 0x52), &ev));
    assert(flx4_map_emit_snapshot(&state, capture_snapshot_event, &capture) == 0);

    assert(flx4_map_message(&state, MSG(0xB0, 0x24, 0x20), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH1_TRIM, (int16_t)((0x10 << 7) | 0x20));
    assert(!flx4_map_message(&state, MSG(0xB1, 0x13, 0x21), &ev));
    assert(flx4_map_message(&state, MSG(0xB1, 0x33, 0x22), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH2_VOLUME, (int16_t)((0x21 << 7) | 0x22));
    assert(flx4_map_message(&state, MSG(0xB1, 0x33, 0x23), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_CH2_VOLUME, (int16_t)((0x21 << 7) | 0x23));
    assert(!flx4_map_message(&state, MSG(0xB6, 0x1F, 0x30), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x3F, 0x31), &ev));
    assert(!flx4_map_message(&state, MSG(0xB6, 0x08, 0x40), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x28, 0x41), &ev));
    assert(!flx4_map_message(&state, MSG(0xB6, 0x0C, 0x50), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x2C, 0x51), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x2D, 0x53), &ev));
    assert(!flx4_map_message(&state, MSG(0xB6, 0x17, 0x60), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x37, 0x61), &ev));
    assert(!flx4_map_message(&state, MSG(0xB0, 0x07, 0x62), &ev));
    assert(flx4_map_message(&state, MSG(0xB0, 0x27, 0x63), &ev));
    assert(!flx4_map_message(&state, MSG(0xB0, 0x0B, 0x64), &ev));
    assert(flx4_map_message(&state, MSG(0xB0, 0x2B, 0x65), &ev));
    assert(!flx4_map_message(&state, MSG(0xB0, 0x0F, 0x66), &ev));
    assert(flx4_map_message(&state, MSG(0xB0, 0x2F, 0x67), &ev));
    assert(flx4_map_message(&state, MSG(0xB0, 0x00, 0x12), &ev) == false);
    assert(flx4_map_message(&state, MSG(0xB0, 0x20, 0x34), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_DECK1_TEMPO, (int16_t)((0x12 << 7) | 0x34));
    assert(flx4_map_message(&state, MSG(0x90, 0x54, 0x7F), &ev));
    assert(flx4_map_message(&state, MSG(0x96, 0x00, 0x7F), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x40, 0x01), &ev));

    size_t emitted = flx4_map_emit_snapshot(&state, capture_snapshot_event, &capture);
    assert(emitted == capture.count);
    assert(emitted == 10);
    expect_snapshot_event(&capture, CTRL_ID_CH1_TRIM, (int16_t)((0x10 << 7) | 0x20));
    expect_snapshot_event(&capture, CTRL_ID_CH2_VOLUME, (int16_t)((0x21 << 7) | 0x23));
    expect_snapshot_event(&capture, CTRL_ID_CROSSFADER, (int16_t)((0x30 << 7) | 0x31));
    expect_snapshot_event(&capture, CTRL_ID_MASTER_VOLUME, (int16_t)((0x40 << 7) | 0x41));
    expect_snapshot_event(&capture, CTRL_ID_HEADPHONE_MIX, (int16_t)((0x50 << 7) | 0x51));
    expect_snapshot_event(&capture, CTRL_ID_HEADPHONE_LEVEL, (int16_t)((0x52 << 7) | 0x53));
    expect_snapshot_event(&capture, CTRL_ID_CH1_FILTER, (int16_t)((0x60 << 7) | 0x61));
    expect_snapshot_event(&capture, CTRL_ID_CH1_EQ_HIGH, (int16_t)((0x62 << 7) | 0x63));
    expect_snapshot_event(&capture, CTRL_ID_CH1_EQ_MID, (int16_t)((0x64 << 7) | 0x65));
    expect_snapshot_event(&capture, CTRL_ID_CH1_EQ_LOW, (int16_t)((0x66 << 7) | 0x67));
    expect_no_snapshot_event(&capture, CTRL_ID_DECK1_TEMPO);
    expect_no_snapshot_event(&capture, CTRL_ID_DECK1_PFL);
    expect_no_snapshot_event(&capture, CTRL_ID_BROWSE_DELTA);
    expect_no_snapshot_event(&capture, CTRL_ID_BEAT_FX_DEPTH);
}

static void test_snapshot_emits_beat_fx_depth_after_observed(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    snapshot_capture_t capture;
    flx4_map_init(&state);
    memset(&capture, 0, sizeof(capture));

    assert(flx4_map_emit_snapshot(&state, capture_snapshot_event, &capture) == 0);
    assert(flx4_map_message(&state, MSG(0xB4, 0x02, 0x46), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_BEAT_FX_DEPTH, 0x46);

    assert(flx4_map_emit_snapshot(&state, capture_snapshot_event, &capture) == 1);
    expect_snapshot_event(&capture, CTRL_ID_BEAT_FX_DEPTH, 0x46);
}

static void test_unsupported_messages_are_ignored(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(!flx4_map_message(NULL, MSG(0x90, 0x0B, 0x7F), &ev));
    assert(!flx4_map_message(&state, NULL, &ev));
    assert(!flx4_map_message(&state, MSG(0x90, 0x6E, 0x7F), &ev));
}

int main(void)
{
    test_transport_load_and_pfl_buttons();
    test_smart_control_buttons();
    test_beat_fx_controls();
    test_deck_transport_extension_buttons();
    test_shifted_extended_deck_actions();
    test_loop_and_beat_jump_buttons();
    test_pad_modes_and_pad_actions();
    test_jog_and_browse_relative_controls();
    test_jog_search_and_master_cue_controls();
    test_14bit_controls_emit_after_both_halves();
    test_snapshot_emits_known_absolute_controls_only();
    test_snapshot_emits_beat_fx_depth_after_observed();
    test_unsupported_messages_are_ignored();
    puts("flx4_map tests passed");
    return 0;
}
