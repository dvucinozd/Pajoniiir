#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "controller_runtime.h"
#include "control_link.h"

static unsigned s_checks;
static flx4_control_event_t s_events[64];
static size_t s_event_count;

#define CHECK(expr) do { \
    s_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void event_cb(const flx4_control_event_t *event, void *ctx)
{
    (void)ctx;
    if (s_event_count < sizeof(s_events) / sizeof(s_events[0])) {
        s_events[s_event_count++] = *event;
    }
}

static usb_midi_message_t midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    return (usb_midi_message_t) {
        .cable = 0,
        .cin = (uint8_t)((status >> 4) & 0x0F),
        .len = 3,
        .status = status,
        .data1 = data1,
        .data2 = data2,
    };
}

int main(void)
{
    const controller_runtime_config_t config = {
        .event_cb = event_cb,
        .callback_ctx = NULL,
    };
    CHECK(controller_runtime_init(&config) == ESP_OK);

    usb_midi_message_t message = midi(0x90, 0x0B, 0x7F);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(s_event_count == 1u);
    CHECK(s_events[0].type == CTRL_TYPE_BUTTON);
    CHECK(s_events[0].id == CTRL_ID_DECK1_PLAY);
    CHECK(s_events[0].value == 1);

    message = midi(0x90, 0x0B, 0x00);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(s_events[1].id == CTRL_ID_DECK1_PLAY);
    CHECK(s_events[1].value == 0);

    message = midi(0xB1, 0x23, 65);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(s_events[2].type == CTRL_TYPE_ENCODER);
    CHECK(s_events[2].id == CTRL_ID_DECK2_JOG_BEND);
    CHECK(s_events[2].value == 1);

    message = midi(0xB0, 0x00, 0x12);
    CHECK(!controller_runtime_handle_midi(&message));
    const size_t before_lsb = s_event_count;
    message = midi(0xB0, 0x20, 0x34);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(s_event_count == before_lsb + 1u);
    CHECK(s_events[s_event_count - 1u].type == CTRL_TYPE_PITCH);
    CHECK(s_events[s_event_count - 1u].id == CTRL_ID_DECK1_TEMPO);
    CHECK(s_events[s_event_count - 1u].value == ((0x12 << 7) | 0x34));

    message = midi(0xB0, 0x13, 0x22);
    CHECK(!controller_runtime_handle_midi(&message));
    message = midi(0xB0, 0x33, 0x11);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(s_events[s_event_count - 1u].id == CTRL_ID_CH1_VOLUME);
    CHECK(s_events[s_event_count - 1u].value == ((0x22 << 7) | 0x11));

    const size_t before_snapshot = s_event_count;
    controller_runtime_set_connected(true);
    CHECK(s_event_count == before_snapshot + 1u);
    CHECK(s_events[s_event_count - 1u].id == CTRL_ID_CH1_VOLUME);
    CHECK(s_events[s_event_count - 1u].value == ((0x22 << 7) | 0x11));

    controller_runtime_diagnostics_t diagnostics;
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.connected);
    CHECK(diagnostics.midi_messages == 7u);
    CHECK(diagnostics.semantic_events == s_event_count);
    CHECK(diagnostics.non_emitting_messages == 2u);
    CHECK(diagnostics.reconnect_snapshots == 1u);

    controller_runtime_set_connected(false);
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(!diagnostics.connected);

    message = midi(0xF0, 0x00, 0x00);
    CHECK(!controller_runtime_handle_midi(&message));
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.non_emitting_messages == 3u);

    printf("PASS P4 local FLX4 controller runtime\n");
    printf("CHECKS=%u EVENTS=%zu\n", s_checks, s_event_count);
    return 0;
}
