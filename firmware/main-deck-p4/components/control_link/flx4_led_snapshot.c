#include "flx4_led_snapshot.h"

#include <string.h>

static const led_id_t s_led_ids[14] = {
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
    case 13:
        return input->loop_active[deck];
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
        for (uint8_t index = 0; index < 14; index++) {
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
