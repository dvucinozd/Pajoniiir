#include "flx4_led_snapshot.h"

#include <string.h>

static const led_id_t s_led_ids[3] = {
    LED_CUE,
    LED_PLAY,
    LED_PFL,
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
    default:
        return input->pfl[deck];
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
        for (uint8_t index = 0; index < 3; index++) {
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
