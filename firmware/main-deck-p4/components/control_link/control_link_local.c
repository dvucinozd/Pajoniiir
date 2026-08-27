/* SPDX-License-Identifier: Apache-2.0 */
#include "control_link.h"

#include <stdatomic.h>

static QueueHandle_t s_event_queue;
static atomic_uint_fast8_t s_sequence;
static control_link_led_sink_fn_t s_led_sink;
static void *s_led_sink_context;

esp_err_t control_link_init(QueueHandle_t ctrl_event_queue)
{
    if (!ctrl_event_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    s_event_queue = ctrl_event_queue;
    atomic_store_explicit(&s_sequence, 0u, memory_order_relaxed);
    return ESP_OK;
}

void control_link_set_led_sink(control_link_led_sink_fn_t sink, void *user_ctx)
{
    s_led_sink = sink;
    s_led_sink_context = user_ctx;
}

void control_link_send_led_deck(led_id_t led, uint8_t state, uint8_t deck)
{
    if (s_led_sink) {
        (void)s_led_sink((uint8_t)led, state, deck, s_led_sink_context);
    }
}

void control_link_send_led(led_id_t led, uint8_t state)
{
    control_link_send_led_deck(led, state, CTRL_DECK_1);
}

esp_err_t control_link_inject_semantic(uint8_t type, uint8_t id, int16_t value)
{
    if (!s_event_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    ctrl_event_t event = {
        .id = id,
        .value = value,
        .seq = atomic_fetch_add_explicit(&s_sequence, 1u, memory_order_relaxed),
        .deck = control_link_id_deck(id),
        .control = control_link_id_control(id),
    };

    switch (type) {
    case CTRL_TYPE_BUTTON:
        event.type = CTRL_EV_BUTTON;
        break;
    case CTRL_TYPE_ENCODER:
        if (id == 0u || control_link_id_is_deck_jog(id)) {
            event.type = CTRL_EV_JOG;
        } else if (id == 1u || id == CTRL_ID_BROWSE_DELTA ||
                   id == CTRL_ID_BROWSE_SHIFT_DELTA) {
            event.type = CTRL_EV_BROWSE;
        } else {
            return ESP_ERR_INVALID_ARG;
        }
        break;
    case CTRL_TYPE_PITCH:
        event.type = CTRL_EV_PITCH;
        break;
    case CTRL_TYPE_STATE:
        event.type = CTRL_EV_STATE;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    return xQueueSend(s_event_queue, &event, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}
