#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "controller_runtime.h"
#include "control_link.h"

static unsigned s_checks;
static flx4_control_event_t s_events[256];
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

static void dispatch_one_and_check(size_t expected_total)
{
    CHECK(controller_runtime_dispatch_pending(1u) == 1u);
    CHECK(s_event_count == expected_total);
}

int main(void)
{
    const controller_runtime_config_t config = {
        .event_cb = event_cb,
        .callback_ctx = NULL,
    };
    CHECK(controller_runtime_init(&config) == ESP_OK);
    CHECK(controller_runtime_pending_count() == 0u);

    usb_midi_message_t message = midi(0x90, 0x0B, 0x7F);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(s_event_count == 0u);
    dispatch_one_and_check(1u);
    CHECK(s_events[0].type == CTRL_TYPE_BUTTON);
    CHECK(s_events[0].id == CTRL_ID_DECK1_PLAY);
    CHECK(s_events[0].value == 1);

    message = midi(0x90, 0x0B, 0x00);
    CHECK(controller_runtime_handle_midi(&message));
    dispatch_one_and_check(2u);
    CHECK(s_events[1].id == CTRL_ID_DECK1_PLAY);
    CHECK(s_events[1].value == 0);

    message = midi(0xB1, 0x23, 65);
    CHECK(controller_runtime_handle_midi(&message));
    dispatch_one_and_check(3u);
    CHECK(s_events[2].type == CTRL_TYPE_ENCODER);
    CHECK(s_events[2].id == CTRL_ID_DECK2_JOG_BEND);
    CHECK(s_events[2].value == 1);

    message = midi(0xB0, 0x00, 0x12);
    CHECK(!controller_runtime_handle_midi(&message));
    message = midi(0xB0, 0x20, 0x34);
    CHECK(controller_runtime_handle_midi(&message));
    dispatch_one_and_check(4u);
    CHECK(s_events[3].type == CTRL_TYPE_PITCH);
    CHECK(s_events[3].id == CTRL_ID_DECK1_TEMPO);
    CHECK(s_events[3].value == ((0x12 << 7) | 0x34));

    message = midi(0xB0, 0x13, 0x22);
    CHECK(!controller_runtime_handle_midi(&message));
    message = midi(0xB0, 0x33, 0x11);
    CHECK(controller_runtime_handle_midi(&message));
    dispatch_one_and_check(5u);
    CHECK(s_events[4].id == CTRL_ID_CH1_VOLUME);
    CHECK(s_events[4].value == ((0x22 << 7) | 0x11));

    controller_runtime_set_connected(true);
    CHECK(controller_runtime_pending_count() == 1u);
    dispatch_one_and_check(6u);
    CHECK(s_events[5].id == CTRL_ID_CH1_VOLUME);
    CHECK(s_events[5].value == ((0x22 << 7) | 0x11));

    controller_runtime_diagnostics_t diagnostics;
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.connected);
    CHECK(diagnostics.midi_messages == 7u);
    CHECK(diagnostics.mapped_messages == 5u);
    CHECK(diagnostics.non_emitting_messages == 2u);
    CHECK(diagnostics.reconnect_snapshots == 1u);
    CHECK(diagnostics.semantic_events == s_event_count);

    message = midi(0xF0, 0x00, 0x00);
    CHECK(!controller_runtime_handle_midi(&message));
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.non_emitting_messages == 3u);

    /* Reinitialize and prove a held physical level survives a completely full
     * bounded queue. The wake token may drop, but the reconciler remains dirty. */
    s_event_count = 0u;
    CHECK(controller_runtime_init(&config) == ESP_OK);
    for (size_t i = 0u; i < 64u; ++i) {
        message = midi(0x90, 0x0B, (uint8_t)((i & 1u) ? 0x7F : 0x00));
        CHECK(controller_runtime_handle_midi(&message));
    }
    CHECK(controller_runtime_pending_count() == 64u);

    message = midi(0x90, 0x36, 0x7F);
    CHECK(controller_runtime_handle_midi(&message));
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.queue_dropped == 1u);
    dispatch_one_and_check(1u);
    CHECK(s_events[0].id == CTRL_ID_DECK1_JOG_TOUCH);
    CHECK(s_events[0].value == 1);

    controller_runtime_set_connected(true);
    controller_runtime_set_connected(false);
    dispatch_one_and_check(2u);
    CHECK(s_events[1].id == CTRL_ID_DECK1_JOG_TOUCH);
    CHECK(s_events[1].value == 0);

    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(!diagnostics.connected);
    CHECK(diagnostics.held_reconciliations == 2u);
    CHECK(diagnostics.semantic_events == 2u);
    CHECK(diagnostics.queued_events == 64u);

    printf("PASS bounded P4 local FLX4 controller runtime\n");
    printf("CHECKS=%u EVENTS=%zu\n", s_checks, s_event_count);
    return 0;
}
