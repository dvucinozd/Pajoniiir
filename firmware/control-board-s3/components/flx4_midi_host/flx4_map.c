#include "flx4_map.h"
#include "control_link.h"

#include <string.h>

#define FLX4_STATUS_D1_BTN     0x90
#define FLX4_STATUS_D2_BTN     0x91
#define FLX4_STATUS_GLOBAL_BTN 0x96
#define FLX4_STATUS_BEAT_FX_CH1 0x94
#define FLX4_STATUS_BEAT_FX_CH2 0x95
#define FLX4_STATUS_PAD_D1     0x97
#define FLX4_STATUS_PAD_D1_SHIFT 0x98
#define FLX4_STATUS_PAD_D2     0x99
#define FLX4_STATUS_PAD_D2_SHIFT 0x9A
#define FLX4_STATUS_D1_CC      0xB0
#define FLX4_STATUS_D2_CC      0xB1
#define FLX4_STATUS_BEAT_FX_CC 0xB4
#define FLX4_STATUS_MASTER_CC  0xB6

#define FLX4_BTN_PLAY          0x0B
#define FLX4_BTN_CENSOR        0x0E
#define FLX4_BTN_CUE           0x0C
#define FLX4_BTN_JOG_TOUCH     0x36
#define FLX4_BTN_SHIFT         0x3F
#define FLX4_BTN_CUE_SHIFT     0x48
#define FLX4_BTN_SYNC          0x58
#define FLX4_BTN_SYNC_MASTER   0x5C
#define FLX4_BTN_TEMPO_RANGE   0x60
#define FLX4_BTN_LOOP_IN       0x10
#define FLX4_BTN_LOOP_OUT      0x11
#define FLX4_BTN_RELOOP_EXIT   0x4D
#define FLX4_BTN_RELOOP_STOP   0x50
#define FLX4_BTN_LOOP_ADJUST_IN 0x4C
#define FLX4_BTN_LOOP_ADJUST_OUT 0x4E
#define FLX4_BTN_LOOP_HALVE    0x51
#define FLX4_BTN_LOOP_DOUBLE   0x53
#define FLX4_BTN_BEAT_JUMP_BACK 0x3E
#define FLX4_BTN_BEAT_JUMP_FORWARD 0x3D
#define FLX4_BTN_QUANTIZE      0x68
#define FLX4_BTN_PAD_HOT_CUE   0x1B
#define FLX4_BTN_PAD_KEYBOARD  0x69
#define FLX4_BTN_PAD_FX1       0x1E
#define FLX4_BTN_PAD_FX2       0x6B
#define FLX4_BTN_PAD_BEAT_LOOP 0x6D
#define FLX4_BTN_PAD_BEAT_JUMP 0x20
#define FLX4_BTN_PAD_SAMPLER   0x22
#define FLX4_BTN_PAD_KEY_SHIFT 0x6F
#define FLX4_BTN_LOAD_D1       0x46
#define FLX4_BTN_LOAD_D2       0x47
#define FLX4_BTN_LOAD_D1_SHIFT 0x68
#define FLX4_BTN_LOAD_D2_SHIFT 0x7A
#define FLX4_BTN_PFL           0x54
#define FLX4_BTN_BROWSE_PRESS  0x41
#define FLX4_BTN_BROWSE_SHIFT_PRESS 0x42
#define FLX4_BTN_SMART_CFX     0x00
#define FLX4_BTN_SMART_FADER   0x01
#define FLX4_BTN_BEAT_FX_TARGET_CH1 0x10
#define FLX4_BTN_BEAT_FX_TARGET_CH2 0x11
#define FLX4_BTN_BEAT_FX_CLEAR 0x43
#define FLX4_BTN_BEAT_FX_ON    0x47
#define FLX4_BTN_BEAT_FX_BEAT_DEC 0x4A
#define FLX4_BTN_BEAT_FX_BEAT_INC 0x4B
#define FLX4_BTN_BEAT_FX_SELECT_NEXT 0x63
#define FLX4_BTN_BEAT_FX_SELECT_PREV 0x64
#define FLX4_BTN_MASTER_CUE     0x63
#define FLX4_BTN_MASTER_CUE_SHIFT 0x78
#define FLX4_BTN_JOG_SEARCH_TOUCH 0x67

#define FLX4_CC_JOG_SIDE_BEND  0x21
#define FLX4_CC_JOG_SCRATCH    0x22
#define FLX4_CC_JOG_BEND       0x23
#define FLX4_CC_JOG_SEARCH     0x29
#define FLX4_CC_TEMPO_MSB      0x00
#define FLX4_CC_TEMPO_LSB      0x20
#define FLX4_CC_CH_VOL_MSB     0x13
#define FLX4_CC_CH_VOL_LSB     0x33
#define FLX4_CC_TRIM_MSB       0x04
#define FLX4_CC_TRIM_LSB       0x24
#define FLX4_CC_EQ_HIGH_MSB    0x07
#define FLX4_CC_EQ_HIGH_LSB    0x27
#define FLX4_CC_EQ_MID_MSB     0x0B
#define FLX4_CC_EQ_MID_LSB     0x2B
#define FLX4_CC_EQ_LOW_MSB     0x0F
#define FLX4_CC_EQ_LOW_LSB     0x2F
#define FLX4_CC_HEADPHONE_MIX_MSB 0x0C
#define FLX4_CC_HEADPHONE_MIX_LSB 0x2C
#define FLX4_CC_MASTER_LEVEL_MSB  0x08
#define FLX4_CC_MASTER_LEVEL_LSB  0x28
#define FLX4_CC_FILTER_CH1_MSB 0x17
#define FLX4_CC_FILTER_CH1_LSB 0x37
#define FLX4_CC_FILTER_CH2_MSB 0x18
#define FLX4_CC_FILTER_CH2_LSB 0x38
#define FLX4_CC_CROSSFADER_MSB 0x1F
#define FLX4_CC_CROSSFADER_LSB 0x3F
#define FLX4_CC_BROWSE         0x40
#define FLX4_CC_BROWSE_SHIFT   0x64
#define FLX4_CC_BEAT_FX_DEPTH  0x02

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

static bool emit_button_value(flx4_control_event_t *out, uint8_t id, int16_t value)
{
    out->type = CTRL_TYPE_BUTTON;
    out->id = id;
    out->value = value;
    return true;
}

static bool emit_deck_ext_action(flx4_control_event_t *out,
                                 bool deck1,
                                 uint8_t action,
                                 uint8_t pressed)
{
    return emit_button_value(out,
                             deck1 ? CTRL_ID_DECK1_EXT_ACTION : CTRL_ID_DECK2_EXT_ACTION,
                             CTRL_DECK_EXT_VALUE(action, pressed != 0));
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

static bool get_14bit_value(const flx4_14bit_state_t *slot, int16_t *out)
{
    if (!slot || !out || !slot->msb_valid || !slot->lsb_valid) {
        return false;
    }
    *out = (int16_t)(((uint16_t)(slot->msb & 0x7F) << 7) | (slot->lsb & 0x7F));
    return true;
}

static int16_t relative_delta(uint8_t value)
{
    return (int16_t)value - 64;
}

static int16_t relative_twos_complement_delta(uint8_t value)
{
    value &= 0x7F;
    if (value == 0x00 || value == 0x40) {
        return 0;
    }
    return value < 0x40 ? (int16_t)value : (int16_t)value - 0x80;
}

static bool map_deck_button(uint8_t status, uint8_t data1, uint8_t data2, flx4_control_event_t *out)
{
    const bool deck1 = status == FLX4_STATUS_D1_BTN;
    uint8_t pressed = data2 > 0 ? 1 : 0;

    switch (data1) {
    case FLX4_BTN_PLAY:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PLAY : CTRL_ID_DECK2_PLAY, pressed);
    case FLX4_BTN_CENSOR:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_CENSOR, pressed);
    case FLX4_BTN_CUE:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_CUE : CTRL_ID_DECK2_CUE, pressed);
    case FLX4_BTN_SHIFT:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_SHIFT : CTRL_ID_DECK2_SHIFT, pressed);
    case FLX4_BTN_CUE_SHIFT:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_TO_START : CTRL_ID_DECK2_TO_START, pressed);
    case FLX4_BTN_SYNC:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_SYNC : CTRL_ID_DECK2_SYNC, pressed);
    case FLX4_BTN_SYNC_MASTER:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_SYNC_MASTER, pressed);
    case FLX4_BTN_TEMPO_RANGE:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_TEMPO_RANGE : CTRL_ID_DECK2_TEMPO_RANGE, pressed);
    case FLX4_BTN_LOOP_IN:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_LOOP_IN : CTRL_ID_DECK2_LOOP_IN, pressed);
    case FLX4_BTN_LOOP_OUT:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_LOOP_OUT : CTRL_ID_DECK2_LOOP_OUT, pressed);
    case FLX4_BTN_RELOOP_EXIT:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_RELOOP_EXIT : CTRL_ID_DECK2_RELOOP_EXIT, pressed);
    case FLX4_BTN_RELOOP_STOP:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_RELOOP_STOP, pressed);
    case FLX4_BTN_LOOP_ADJUST_IN:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, pressed);
    case FLX4_BTN_LOOP_ADJUST_OUT:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT, pressed);
    case FLX4_BTN_LOOP_HALVE:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_LOOP_HALVE : CTRL_ID_DECK2_LOOP_HALVE, pressed);
    case FLX4_BTN_LOOP_DOUBLE:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_LOOP_DOUBLE : CTRL_ID_DECK2_LOOP_DOUBLE, pressed);
    case FLX4_BTN_BEAT_JUMP_BACK:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_BEAT_JUMP_BACK : CTRL_ID_DECK2_BEAT_JUMP_BACK, pressed);
    case FLX4_BTN_BEAT_JUMP_FORWARD:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_BEAT_JUMP_FORWARD : CTRL_ID_DECK2_BEAT_JUMP_FORWARD, pressed);
    case FLX4_BTN_QUANTIZE:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_QUANTIZE, pressed);
    case FLX4_BTN_PAD_HOT_CUE:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_HOT_CUE : CTRL_ID_DECK2_PAD_MODE_HOT_CUE, pressed);
    case FLX4_BTN_PAD_KEYBOARD:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_KEYBOARD : CTRL_ID_DECK2_PAD_MODE_KEYBOARD, pressed);
    case FLX4_BTN_PAD_FX1:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_PAD_FX1 : CTRL_ID_DECK2_PAD_MODE_PAD_FX1, pressed);
    case FLX4_BTN_PAD_FX2:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_PAD_FX2 : CTRL_ID_DECK2_PAD_MODE_PAD_FX2, pressed);
    case FLX4_BTN_PAD_BEAT_LOOP:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_BEAT_LOOP : CTRL_ID_DECK2_PAD_MODE_BEAT_LOOP, pressed);
    case FLX4_BTN_PAD_BEAT_JUMP:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_BEAT_JUMP : CTRL_ID_DECK2_PAD_MODE_BEAT_JUMP, pressed);
    case FLX4_BTN_PAD_SAMPLER:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_SAMPLER : CTRL_ID_DECK2_PAD_MODE_SAMPLER, pressed);
    case FLX4_BTN_PAD_KEY_SHIFT:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_PAD_MODE_KEY_SHIFT : CTRL_ID_DECK2_PAD_MODE_KEY_SHIFT, pressed);
    case FLX4_BTN_JOG_TOUCH:
        return emit_button(out, deck1 ? CTRL_ID_DECK1_JOG_TOUCH : CTRL_ID_DECK2_JOG_TOUCH, pressed);
    case FLX4_BTN_JOG_SEARCH_TOUCH:
        return emit_button(out,
                           deck1 ? CTRL_ID_DECK1_JOG_SEARCH_TOUCH : CTRL_ID_DECK2_JOG_SEARCH_TOUCH,
                           pressed);
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
    case FLX4_CC_JOG_SEARCH:
        return emit_encoder(out,
                            deck1 ? CTRL_ID_DECK1_JOG_SEARCH : CTRL_ID_DECK2_JOG_SEARCH,
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
    case FLX4_CC_TRIM_MSB:
        return update_14bit(&state->trim[deck], true, data2, out,
                            deck1 ? CTRL_ID_CH1_TRIM : CTRL_ID_CH2_TRIM);
    case FLX4_CC_TRIM_LSB:
        return update_14bit(&state->trim[deck], false, data2, out,
                            deck1 ? CTRL_ID_CH1_TRIM : CTRL_ID_CH2_TRIM);
    case FLX4_CC_EQ_HIGH_MSB:
        return update_14bit(&state->eq_high[deck], true, data2, out,
                            deck1 ? CTRL_ID_CH1_EQ_HIGH : CTRL_ID_CH2_EQ_HIGH);
    case FLX4_CC_EQ_HIGH_LSB:
        return update_14bit(&state->eq_high[deck], false, data2, out,
                            deck1 ? CTRL_ID_CH1_EQ_HIGH : CTRL_ID_CH2_EQ_HIGH);
    case FLX4_CC_EQ_MID_MSB:
        return update_14bit(&state->eq_mid[deck], true, data2, out,
                            deck1 ? CTRL_ID_CH1_EQ_MID : CTRL_ID_CH2_EQ_MID);
    case FLX4_CC_EQ_MID_LSB:
        return update_14bit(&state->eq_mid[deck], false, data2, out,
                            deck1 ? CTRL_ID_CH1_EQ_MID : CTRL_ID_CH2_EQ_MID);
    case FLX4_CC_EQ_LOW_MSB:
        return update_14bit(&state->eq_low[deck], true, data2, out,
                            deck1 ? CTRL_ID_CH1_EQ_LOW : CTRL_ID_CH2_EQ_LOW);
    case FLX4_CC_EQ_LOW_LSB:
        return update_14bit(&state->eq_low[deck], false, data2, out,
                            deck1 ? CTRL_ID_CH1_EQ_LOW : CTRL_ID_CH2_EQ_LOW);
    default:
        return false;
    }
}

static bool emit_beat_fx_target_from_state(const flx4_map_state_t *state, flx4_control_event_t *out)
{
    if (state->beat_fx_target_ch1 && state->beat_fx_target_ch2) {
        return emit_button_value(out, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_BOTH);
    }
    if (state->beat_fx_target_ch1) {
        return emit_button_value(out, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    }
    if (state->beat_fx_target_ch2) {
        return emit_button_value(out, CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH2);
    }
    return false;
}

static bool map_beat_fx_button(flx4_map_state_t *state,
                               uint8_t status,
                               uint8_t data1,
                               uint8_t data2,
                               flx4_control_event_t *out)
{
    uint8_t pressed = data2 > 0 ? 1 : 0;

    switch (data1) {
    case FLX4_BTN_BEAT_FX_SELECT_NEXT:
        return emit_button(out, CTRL_ID_BEAT_FX_SELECT_NEXT, pressed);
    case FLX4_BTN_BEAT_FX_SELECT_PREV:
        return emit_button(out, CTRL_ID_BEAT_FX_SELECT_PREV, pressed);
    case FLX4_BTN_BEAT_FX_BEAT_DEC:
        return emit_button(out, CTRL_ID_BEAT_FX_BEAT_DEC, pressed);
    case FLX4_BTN_BEAT_FX_BEAT_INC:
        return emit_button(out, CTRL_ID_BEAT_FX_BEAT_INC, pressed);
    case FLX4_BTN_BEAT_FX_ON:
        return emit_button(out, CTRL_ID_BEAT_FX_ON, pressed);
    case FLX4_BTN_BEAT_FX_CLEAR:
        return emit_button(out, CTRL_ID_BEAT_FX_CLEAR, pressed);
    case FLX4_BTN_BEAT_FX_TARGET_CH1:
        if (status != FLX4_STATUS_BEAT_FX_CH1) {
            return false;
        }
        state->beat_fx_target_ch1 = pressed != 0;
        return emit_beat_fx_target_from_state(state, out);
    case FLX4_BTN_BEAT_FX_TARGET_CH2:
        if (status != FLX4_STATUS_BEAT_FX_CH2) {
            return false;
        }
        state->beat_fx_target_ch2 = pressed != 0;
        return emit_beat_fx_target_from_state(state, out);
    default:
        return false;
    }
}

static bool map_beat_fx_cc(flx4_map_state_t *state,
                           uint8_t data1,
                           uint8_t data2,
                           flx4_control_event_t *out)
{
    if (data1 != FLX4_CC_BEAT_FX_DEPTH) {
        return false;
    }
    state->beat_fx_depth = data2 & 0x7F;
    state->beat_fx_depth_valid = true;
    out->type = CTRL_TYPE_PITCH;
    out->id = CTRL_ID_BEAT_FX_DEPTH;
    out->value = (int16_t)state->beat_fx_depth;
    return true;
}

static bool map_master_cc(flx4_map_state_t *state,
                          uint8_t data1,
                          uint8_t data2,
                          flx4_control_event_t *out)
{
    switch (data1) {
    case FLX4_CC_BROWSE:
        return emit_encoder(out, CTRL_ID_BROWSE_DELTA, relative_twos_complement_delta(data2));
    case FLX4_CC_BROWSE_SHIFT:
        return emit_encoder(out, CTRL_ID_BROWSE_SHIFT_DELTA, relative_twos_complement_delta(data2));
    case FLX4_CC_CROSSFADER_MSB:
        return update_14bit(&state->crossfader, true, data2, out, CTRL_ID_CROSSFADER);
    case FLX4_CC_CROSSFADER_LSB:
        return update_14bit(&state->crossfader, false, data2, out, CTRL_ID_CROSSFADER);
    case FLX4_CC_HEADPHONE_MIX_MSB:
        return update_14bit(&state->headphone_mix, true, data2, out, CTRL_ID_HEADPHONE_MIX);
    case FLX4_CC_HEADPHONE_MIX_LSB:
        return update_14bit(&state->headphone_mix, false, data2, out, CTRL_ID_HEADPHONE_MIX);
    case FLX4_CC_MASTER_LEVEL_MSB:
        return update_14bit(&state->master_volume, true, data2, out, CTRL_ID_MASTER_VOLUME);
    case FLX4_CC_MASTER_LEVEL_LSB:
        return update_14bit(&state->master_volume, false, data2, out, CTRL_ID_MASTER_VOLUME);
    case FLX4_CC_FILTER_CH1_MSB:
        return update_14bit(&state->filter[CTRL_DECK_1], true, data2, out, CTRL_ID_CH1_FILTER);
    case FLX4_CC_FILTER_CH1_LSB:
        return update_14bit(&state->filter[CTRL_DECK_1], false, data2, out, CTRL_ID_CH1_FILTER);
    case FLX4_CC_FILTER_CH2_MSB:
        return update_14bit(&state->filter[CTRL_DECK_2], true, data2, out, CTRL_ID_CH2_FILTER);
    case FLX4_CC_FILTER_CH2_LSB:
        return update_14bit(&state->filter[CTRL_DECK_2], false, data2, out, CTRL_ID_CH2_FILTER);
    default:
        return false;
    }
}

static bool map_pad_message(uint8_t status, uint8_t data1, uint8_t data2, flx4_control_event_t *out)
{
    bool deck1 = (status == FLX4_STATUS_PAD_D1 || status == FLX4_STATUS_PAD_D1_SHIFT);
    bool shifted = (status == FLX4_STATUS_PAD_D1_SHIFT || status == FLX4_STATUS_PAD_D2_SHIFT);
    uint8_t mode;
    uint8_t pad;

    if (data1 <= 0x07) {
        mode = CTRL_PAD_MODE_HOT_CUE;
        pad = data1;
    } else if (data1 >= 0x10 && data1 <= 0x17) {
        mode = CTRL_PAD_MODE_PAD_FX1;
        pad = data1 - 0x10;
    } else if (data1 >= 0x20 && data1 <= 0x27) {
        mode = CTRL_PAD_MODE_BEAT_JUMP;
        pad = data1 - 0x20;
    } else if (data1 >= 0x30 && data1 <= 0x37) {
        mode = CTRL_PAD_MODE_SAMPLER;
        pad = data1 - 0x30;
    } else if (data1 >= 0x40 && data1 <= 0x47) {
        mode = CTRL_PAD_MODE_KEYBOARD;
        pad = data1 - 0x40;
    } else if (data1 >= 0x50 && data1 <= 0x57) {
        mode = CTRL_PAD_MODE_PAD_FX2;
        pad = data1 - 0x50;
    } else if (data1 >= 0x60 && data1 <= 0x67) {
        mode = CTRL_PAD_MODE_BEAT_LOOP;
        pad = data1 - 0x60;
    } else if (data1 >= 0x70 && data1 <= 0x77) {
        mode = CTRL_PAD_MODE_KEY_SHIFT;
        pad = data1 - 0x70;
    } else {
        return false;
    }

    return emit_button_value(out,
                             deck1 ? CTRL_ID_DECK1_PAD_ACTION : CTRL_ID_DECK2_PAD_ACTION,
                             CTRL_PAD_ACTION_VALUE(mode, pad, shifted, data2 > 0));
}

bool flx4_map_message(flx4_map_state_t *state,
                      const flx4_midi_message_t *msg,
                      flx4_control_event_t *out)
{
    if (!state || !msg || !out || msg->len < 3) {
        return false;
    }

    switch (msg->status) {
    case FLX4_STATUS_BEAT_FX_CH1:
    case FLX4_STATUS_BEAT_FX_CH2:
        return map_beat_fx_button(state, msg->status, msg->data1, msg->data2, out);
    case FLX4_STATUS_D1_BTN:
    case FLX4_STATUS_D2_BTN:
        return map_deck_button(msg->status, msg->data1, msg->data2, out);
    case FLX4_STATUS_GLOBAL_BTN:
        if (msg->data1 == FLX4_BTN_SMART_CFX) {
            return emit_button(out, CTRL_ID_SMART_CFX, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_SMART_FADER) {
            return emit_button(out, CTRL_ID_SMART_FADER, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_BROWSE_PRESS) {
            return emit_button(out, CTRL_ID_BROWSE_PRESS, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_BROWSE_SHIFT_PRESS) {
            return emit_button(out, CTRL_ID_BROWSE_SHIFT_PRESS, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_LOAD_D1) {
            return emit_button(out, CTRL_ID_LOAD_DECK1, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_LOAD_D2) {
            return emit_button(out, CTRL_ID_LOAD_DECK2, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_LOAD_D1_SHIFT) {
            return emit_button(out, CTRL_ID_SHIFT_LOAD_DECK1, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_LOAD_D2_SHIFT) {
            return emit_button(out, CTRL_ID_SHIFT_LOAD_DECK2, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_MASTER_CUE ||
            msg->data1 == FLX4_BTN_MASTER_CUE_SHIFT) {
            return emit_button(out, CTRL_ID_MASTER_CUE, msg->data2 > 0 ? 1 : 0);
        }
        return false;
    case FLX4_STATUS_PAD_D1:
    case FLX4_STATUS_PAD_D1_SHIFT:
    case FLX4_STATUS_PAD_D2:
    case FLX4_STATUS_PAD_D2_SHIFT:
        return map_pad_message(msg->status, msg->data1, msg->data2, out);
    case FLX4_STATUS_D1_CC:
    case FLX4_STATUS_D2_CC:
        return map_deck_cc(state, msg->status, msg->data1, msg->data2, out);
    case FLX4_STATUS_BEAT_FX_CC:
        return map_beat_fx_cc(state, msg->data1, msg->data2, out);
    case FLX4_STATUS_MASTER_CC:
        return map_master_cc(state, msg->data1, msg->data2, out);
    default:
        return false;
    }
}

static bool emit_snapshot_14bit(const flx4_14bit_state_t *slot,
                                uint8_t id,
                                flx4_map_snapshot_emit_cb_t cb,
                                void *ctx,
                                size_t *count)
{
    int16_t value;
    if (!get_14bit_value(slot, &value)) {
        return true;
    }
    if (!cb(CTRL_TYPE_PITCH, id, value, ctx)) {
        return false;
    }
    (*count)++;
    return true;
}

size_t flx4_map_emit_snapshot(const flx4_map_state_t *state,
                              flx4_map_snapshot_emit_cb_t cb,
                              void *ctx)
{
    if (!state || !cb) {
        return 0;
    }

    size_t count = 0;

    if (!emit_snapshot_14bit(&state->channel_volume[CTRL_DECK_1],
                             CTRL_ID_CH1_VOLUME, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->channel_volume[CTRL_DECK_2],
                             CTRL_ID_CH2_VOLUME, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->crossfader,
                             CTRL_ID_CROSSFADER, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->trim[CTRL_DECK_1],
                             CTRL_ID_CH1_TRIM, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->trim[CTRL_DECK_2],
                             CTRL_ID_CH2_TRIM, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->eq_high[CTRL_DECK_1],
                             CTRL_ID_CH1_EQ_HIGH, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->eq_high[CTRL_DECK_2],
                             CTRL_ID_CH2_EQ_HIGH, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->eq_mid[CTRL_DECK_1],
                             CTRL_ID_CH1_EQ_MID, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->eq_mid[CTRL_DECK_2],
                             CTRL_ID_CH2_EQ_MID, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->eq_low[CTRL_DECK_1],
                             CTRL_ID_CH1_EQ_LOW, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->eq_low[CTRL_DECK_2],
                             CTRL_ID_CH2_EQ_LOW, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->filter[CTRL_DECK_1],
                             CTRL_ID_CH1_FILTER, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->filter[CTRL_DECK_2],
                             CTRL_ID_CH2_FILTER, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->master_volume,
                             CTRL_ID_MASTER_VOLUME, cb, ctx, &count) ||
        !emit_snapshot_14bit(&state->headphone_mix,
                             CTRL_ID_HEADPHONE_MIX, cb, ctx, &count)) {
        return count;
    }

    if (state->beat_fx_depth_valid) {
        if (!cb(CTRL_TYPE_PITCH, CTRL_ID_BEAT_FX_DEPTH,
                (int16_t)(state->beat_fx_depth & 0x7F), ctx)) {
            return count;
        }
        count++;
    }

    return count;
}
