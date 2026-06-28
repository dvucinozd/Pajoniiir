#include "flx4_led_snapshot.h"

#include <string.h>

#define FLX4_LED_SNAPSHOT_COUNT 24u

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
    LED_SMART_CFX,
    LED_SMART_FADER,
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

static const uint32_t s_beat_loop_durations_ms[8] = {
    16, 32, 63, 125, 250, 500, 1000, 2000,
};

static bool duration_matches(uint32_t actual_ms, uint32_t expected_ms)
{
    uint32_t tolerance_ms = expected_ms / 16u;
    if (tolerance_ms < 2u) {
        tolerance_ms = 2u;
    }
    uint32_t delta = actual_ms > expected_ms ? actual_ms - expected_ms : expected_ms - actual_ms;
    return delta <= tolerance_ms;
}

static uint8_t beat_loop_pad_value(const flx4_led_snapshot_input_t *input,
                                   uint8_t deck,
                                   uint8_t pad)
{
    if (input->pad_mode[deck] != CTRL_PAD_MODE_BEAT_LOOP ||
        !input->loop_active[deck] ||
        input->loop_end_ms[deck] <= input->loop_start_ms[deck] ||
        pad >= 8u) {
        return 0;
    }

    uint32_t duration_ms = input->loop_end_ms[deck] - input->loop_start_ms[deck];
    return duration_matches(duration_ms, s_beat_loop_durations_ms[pad]) ? 1u : 0u;
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
        return input->smart_cfx;
    case 23:
        return input->smart_fader;
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
                (s_led_ids[index] == LED_SMART_CFX || s_led_ids[index] == LED_SMART_FADER)) {
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
