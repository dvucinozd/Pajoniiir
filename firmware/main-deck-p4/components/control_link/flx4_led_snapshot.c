#include "flx4_led_snapshot.h"

#include <string.h>

#define FLX4_LED_SNAPSHOT_COUNT 49u

static const led_id_t s_led_ids[FLX4_LED_SNAPSHOT_COUNT] = {
    LED_CUE,
    LED_PLAY,
    LED_PFL,
    LED_SYNC,
    LED_PAD_MODE_HOT_CUE,
    LED_PAD_MODE_KEYBOARD,
    LED_PAD_MODE_PAD_FX1,
    LED_PAD_MODE_PAD_FX2,
    LED_PAD_MODE_BEAT_JUMP,
    LED_PAD_MODE_BEAT_LOOP,
    LED_PAD_MODE_SAMPLER,
    LED_PAD_MODE_KEY_SHIFT,
    LED_LOOP_IN,
    LED_LOOP_OUT,
    LED_BEAT_LOOP_PAD_1,
    LED_BEAT_LOOP_PAD_2,
    LED_BEAT_LOOP_PAD_3,
    LED_BEAT_LOOP_PAD_4,
    LED_BEAT_LOOP_PAD_5,
    LED_BEAT_LOOP_PAD_6,
    LED_BEAT_LOOP_PAD_7,
    LED_BEAT_LOOP_PAD_8,
    LED_PAD_FX1_PAD_1,
    LED_PAD_FX1_PAD_2,
    LED_PAD_FX1_PAD_3,
    LED_PAD_FX1_PAD_4,
    LED_PAD_FX1_PAD_5,
    LED_PAD_FX1_PAD_6,
    LED_PAD_FX1_PAD_7,
    LED_PAD_FX1_PAD_8,
    LED_PAD_FX2_PAD_1,
    LED_PAD_FX2_PAD_2,
    LED_PAD_FX2_PAD_3,
    LED_PAD_FX2_PAD_4,
    LED_PAD_FX2_PAD_5,
    LED_PAD_FX2_PAD_6,
    LED_PAD_FX2_PAD_7,
    LED_PAD_FX2_PAD_8,
    LED_SMART_CFX,
    LED_SMART_FADER,
    LED_BEAT_FX_ON,
    LED_HOT_CUE_PAD_1,
    LED_HOT_CUE_PAD_2,
    LED_HOT_CUE_PAD_3,
    LED_HOT_CUE_PAD_4,
    LED_HOT_CUE_PAD_5,
    LED_HOT_CUE_PAD_6,
    LED_HOT_CUE_PAD_7,
    LED_HOT_CUE_PAD_8,
};

static const uint8_t s_pad_modes[8] = {
    CTRL_PAD_MODE_HOT_CUE,
    CTRL_PAD_MODE_KEYBOARD,
    CTRL_PAD_MODE_PAD_FX1,
    CTRL_PAD_MODE_PAD_FX2,
    CTRL_PAD_MODE_BEAT_JUMP,
    CTRL_PAD_MODE_BEAT_LOOP,
    CTRL_PAD_MODE_SAMPLER,
    CTRL_PAD_MODE_KEY_SHIFT,
};

static uint8_t beat_loop_pad_value(const flx4_led_snapshot_input_t *input,
                                   uint8_t deck,
                                   uint8_t pad)
{
    if (input->pad_mode[deck] != CTRL_PAD_MODE_BEAT_LOOP ||
        !input->loop_active[deck] ||
        !input->beat_loop_pad_active[deck] ||
        input->beat_loop_active_pad[deck] != pad ||
        pad >= 8u) {
        return 0;
    }
    return 1u;
}

static uint8_t pad_fx_pad_value(const flx4_led_snapshot_input_t *input,
                                uint8_t deck,
                                uint8_t mode,
                                uint8_t pad)
{
    if (!input->pad_fx_active[deck] ||
        input->pad_fx_active_mode[deck] != mode ||
        input->pad_fx_active_pad[deck] != pad ||
        input->pad_mode[deck] != mode ||
        pad >= 8u) {
        return 0;
    }
    return 1u;
}

static uint8_t hot_cue_pad_value(const flx4_led_snapshot_input_t *input,
                                 uint8_t deck,
                                 uint8_t pad)
{
    if (input->pad_mode[deck] != CTRL_PAD_MODE_HOT_CUE || pad >= 8u) {
        return 0;
    }
    return (input->hot_cue_exists_mask[deck] & (uint8_t)(1u << pad)) ? 1u : 0u;
}

static uint8_t snapshot_value(const flx4_led_snapshot_input_t *input,
                              uint8_t deck,
                              uint8_t index)
{
    switch (index) {
    case 0:
        return input->cue[deck];
    case 1:
        return input->play[deck];
    case 2:
        return input->pfl[deck];
    case 3:
        return input->sync[deck];
    case 12:
        return input->loop_in_marker[deck] || input->loop_active[deck];
    case 13:
        return input->loop_active[deck];
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
        return beat_loop_pad_value(input, deck, (uint8_t)(index - 14u));
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
        return pad_fx_pad_value(input, deck, CTRL_PAD_MODE_PAD_FX1, (uint8_t)(index - 22u));
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
        return pad_fx_pad_value(input, deck, CTRL_PAD_MODE_PAD_FX2, (uint8_t)(index - 30u));
    case 38:
        return input->smart_cfx;
    case 39:
        return input->smart_fader;
    case 40:
        return input->beat_fx_on;
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
        return hot_cue_pad_value(input, deck, (uint8_t)(index - 41u));
    default: {
        uint8_t pad_index = (uint8_t)(index - 4u);
        if (pad_index >= 8u) {
            return 0;
        }
        return input->pad_mode[deck] == s_pad_modes[pad_index] ? 1u : 0u;
    }
    }
}

void flx4_led_publisher_init(flx4_led_publisher_t *publisher)
{
    if (publisher) {
        memset(publisher, 0, sizeof(*publisher));
    }
}

esp_err_t flx4_led_publisher_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx)
{
    if (!publisher || !input || !send) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t first_error = ESP_OK;
    for (uint8_t deck = 0; deck < 2; deck++) {
        for (uint8_t index = 0; index < FLX4_LED_SNAPSHOT_COUNT; index++) {
            if (deck == CTRL_DECK_2 &&
                (s_led_ids[index] == LED_SMART_CFX ||
                 s_led_ids[index] == LED_SMART_FADER ||
                 s_led_ids[index] == LED_BEAT_FX_ON)) {
                continue;
            }
            uint8_t value = snapshot_value(input, deck, index);
            if (!force &&
                publisher->valid[deck][index] &&
                publisher->last[deck][index] == value) {
                continue;
            }

            esp_err_t rc = send(s_led_ids[index], value, deck, ctx);
            if (rc == ESP_OK) {
                publisher->last[deck][index] = value;
                publisher->valid[deck][index] = true;
            } else if (first_error == ESP_OK) {
                first_error = rc;
            }
        }
    }
    return first_error;
}
