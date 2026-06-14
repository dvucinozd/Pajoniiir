#include "flx4_map.h"
#include "control_link.h"

#include <string.h>

#define FLX4_STATUS_D1_BTN     0x90
#define FLX4_STATUS_D2_BTN     0x91
#define FLX4_STATUS_GLOBAL_BTN 0x96
#define FLX4_STATUS_D1_CC      0xB0
#define FLX4_STATUS_D2_CC      0xB1
#define FLX4_STATUS_MASTER_CC  0xB6

#define FLX4_BTN_PLAY          0x0B
#define FLX4_BTN_CUE           0x0C
#define FLX4_BTN_JOG_TOUCH     0x36
#define FLX4_BTN_LOAD_D1       0x46
#define FLX4_BTN_LOAD_D2       0x47
#define FLX4_BTN_PFL           0x54

#define FLX4_CC_JOG_SIDE_BEND  0x21
#define FLX4_CC_JOG_SCRATCH    0x22
#define FLX4_CC_JOG_BEND       0x23
#define FLX4_CC_TEMPO_MSB      0x00
#define FLX4_CC_TEMPO_LSB      0x20
#define FLX4_CC_CH_VOL_MSB     0x13
#define FLX4_CC_CH_VOL_LSB     0x33
#define FLX4_CC_CROSSFADER_MSB 0x1F
#define FLX4_CC_CROSSFADER_LSB 0x3F
#define FLX4_CC_BROWSE         0x40

void flx4_map_init(flx4_map_state_t *state)
{
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}

static bool emit_button(flx4_control_event_t *out, uint8_t id, uint8_t value)
{
    out->type = CTRL_TYPE_BUTTON;
    out->id = id;
    out->value = value ? 1 : 0;
    return true;
}

static bool emit_encoder(flx4_control_event_t *out, uint8_t id, int16_t value)
{
    if (value == 0) {
        return false;
    }
    out->type = CTRL_TYPE_ENCODER;
    out->id = id;
    out->value = value;
    return true;
}

static bool update_14bit(flx4_14bit_state_t *slot,
                         bool is_msb,
                         uint8_t data,
                         flx4_control_event_t *out,
                         uint8_t id)
{
    data &= 0x7F;
    if (is_msb) {
        slot->msb = data;
        slot->msb_valid = true;
    } else {
        slot->lsb = data;
        slot->lsb_valid = true;
    }
    if (!slot->msb_valid || !slot->lsb_valid) {
        return false;
    }

    out->type = CTRL_TYPE_PITCH;
    out->id = id;
    out->value = (int16_t)(((uint16_t)slot->msb << 7) | slot->lsb);
    return true;
}

static int16_t relative_delta(uint8_t value)
{
    return (int16_t)value - 64;
}

static bool map_deck_button(uint8_t status, uint8_t data1, uint8_t data2, flx4_control_event_t *out)
{
    const bool deck1 = status == FLX4_STATUS_D1_BTN;
    uint8_t pressed = data2 > 0 ? 1 : 0;

    switch (data1) {
    case FLX4_BTN_PLAY:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PLAY : CTRL_ID_DECK2_PLAY, pressed);
    case FLX4_BTN_CUE:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_CUE : CTRL_ID_DECK2_CUE, pressed);
    case FLX4_BTN_JOG_TOUCH:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_JOG_TOUCH : CTRL_ID_DECK2_JOG_TOUCH, pressed);
    case FLX4_BTN_PFL:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PFL : CTRL_ID_DECK2_PFL, pressed);
    default:
        return false;
    }
}

static bool map_deck_cc(flx4_map_state_t *state,
                        uint8_t status,
                        uint8_t data1,
                        uint8_t data2,
                        flx4_control_event_t *out)
{
    const uint8_t deck = (status == FLX4_STATUS_D1_CC) ? 0 : 1;
    const bool deck1 = deck == 0;

    switch (data1) {
    case FLX4_CC_JOG_SCRATCH:
        return emit_encoder(out,
                            deck1 ? CTRL_ID_DECK1_JOG_SCRATCH : CTRL_ID_DECK2_JOG_SCRATCH,
                            relative_delta(data2));
    case FLX4_CC_JOG_BEND:
    case FLX4_CC_JOG_SIDE_BEND:
        return emit_encoder(out,
                            deck1 ? CTRL_ID_DECK1_JOG_BEND : CTRL_ID_DECK2_JOG_BEND,
                            relative_delta(data2));
    case FLX4_CC_TEMPO_MSB:
        return update_14bit(&state->tempo[deck], true, data2, out,
                            deck1 ? CTRL_ID_DECK1_TEMPO : CTRL_ID_DECK2_TEMPO);
    case FLX4_CC_TEMPO_LSB:
        return update_14bit(&state->tempo[deck], false, data2, out,
                            deck1 ? CTRL_ID_DECK1_TEMPO : CTRL_ID_DECK2_TEMPO);
    case FLX4_CC_CH_VOL_MSB:
        return update_14bit(&state->channel_volume[deck], true, data2, out,
                            deck1 ? CTRL_ID_CH1_VOLUME : CTRL_ID_CH2_VOLUME);
    case FLX4_CC_CH_VOL_LSB:
        return update_14bit(&state->channel_volume[deck], false, data2, out,
                            deck1 ? CTRL_ID_CH1_VOLUME : CTRL_ID_CH2_VOLUME);
    default:
        return false;
    }
}

static bool map_master_cc(flx4_map_state_t *state,
                          uint8_t data1,
                          uint8_t data2,
                          flx4_control_event_t *out)
{
    switch (data1) {
    case FLX4_CC_BROWSE:
        return emit_encoder(out, CTRL_ID_BROWSE_DELTA, relative_delta(data2));
    case FLX4_CC_CROSSFADER_MSB:
        return update_14bit(&state->crossfader, true, data2, out, CTRL_ID_CROSSFADER);
    case FLX4_CC_CROSSFADER_LSB:
        return update_14bit(&state->crossfader, false, data2, out, CTRL_ID_CROSSFADER);
    default:
        return false;
    }
}

bool flx4_map_message(flx4_map_state_t *state,
                      const flx4_midi_message_t *msg,
                      flx4_control_event_t *out)
{
    if (!state || !msg || !out || msg->len < 3) {
        return false;
    }

    switch (msg->status) {
    case FLX4_STATUS_D1_BTN:
    case FLX4_STATUS_D2_BTN:
        return map_deck_button(msg->status, msg->data1, msg->data2, out);
    case FLX4_STATUS_GLOBAL_BTN:
        if (msg->data1 == FLX4_BTN_LOAD_D1) {
            return emit_button(out, CTRL_ID_LOAD_DECK1, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_LOAD_D2) {
            return emit_button(out, CTRL_ID_LOAD_DECK2, msg->data2 > 0 ? 1 : 0);
        }
        return false;
    case FLX4_STATUS_D1_CC:
    case FLX4_STATUS_D2_CC:
        return map_deck_cc(state, msg->status, msg->data1, msg->data2, out);
    case FLX4_STATUS_MASTER_CC:
        return map_master_cc(state, msg->data1, msg->data2, out);
    default:
        return false;
    }
}
