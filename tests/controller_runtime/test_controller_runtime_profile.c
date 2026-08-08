#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller_profile.h"
#include "controller_profile_runtime.h"
#include "controller_runtime.h"
#include "control_link.h"

static unsigned s_checks;
static flx4_control_event_t s_events[256];
static size_t s_event_count;

#define LARGE_REPLAY_COUNT 80u

#define CHECK(expr) do { \
    s_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void wr_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)(value >> 8u);
}

static void wr_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8u) & 0xFFu);
    p[2] = (uint8_t)((value >> 16u) & 0xFFu);
    p[3] = (uint8_t)(value >> 24u);
}

static size_t build_profile(uint8_t blob[CP_HEADER_SIZE +
                                         CP_INPUT_ENTRY_SIZE +
                                         CP_OUTPUT_ENTRY_SIZE])
{
    memset(blob, 0, CP_HEADER_SIZE + CP_INPUT_ENTRY_SIZE +
                    CP_OUTPUT_ENTRY_SIZE);
    const size_t total = CP_HEADER_SIZE + CP_INPUT_ENTRY_SIZE +
                         CP_OUTPUT_ENTRY_SIZE;

    uint8_t *input = blob + CP_HEADER_SIZE;
    input[0] = 0xB0;
    input[1] = 0x55;
    input[2] = CP_IN_CC7_ABS;
    input[3] = CP_PAIR_SLOT_NONE;
    input[4] = CTRL_TYPE_PITCH;
    input[5] = CTRL_ID_CH1_VOLUME;
    wr_u16(input + 6, CP_IN_FLAG_REPLAY);
    wr_u16(input + 8, 0u);
    wr_u16(input + 10, 0u);
    memset(input + 12, 0xFF, 4u);

    uint8_t *output = input + CP_INPUT_ENTRY_SIZE;
    output[0] = 1u;
    output[1] = 0u;
    output[2] = CP_OUT_NOTE_ONOFF;
    output[3] = 0x90u;
    output[4] = 0x0Bu;
    output[5] = 0x00u;
    output[6] = 0x7Fu;
    output[7] = 0x11u;

    memcpy(blob, CP_MAGIC, 4u);
    wr_u16(blob + 4, CP_VERSION);
    wr_u16(blob + 6, CP_HEADER_SIZE);
    wr_u32(blob + 8, (uint32_t)total);
    wr_u16(blob + 16, 0x1234u);
    wr_u16(blob + 18, 0x5678u);
    wr_u32(blob + 20, CP_PF_LED_FEEDBACK);
    wr_u16(blob + 24, 1u);
    wr_u16(blob + 26, 1u);
    blob[28] = 0u;
    blob[29] = 2u;
    wr_u32(blob + 12, cp_crc32(blob + 16, total - 16u));
    return total;
}

static size_t build_large_replay_profile(uint8_t *blob)
{
    const size_t total = CP_HEADER_SIZE +
                         LARGE_REPLAY_COUNT * CP_INPUT_ENTRY_SIZE;
    memset(blob, 0, total);
    for (size_t i = 0u; i < LARGE_REPLAY_COUNT; ++i) {
        uint8_t *input = blob + CP_HEADER_SIZE + i * CP_INPUT_ENTRY_SIZE;
        input[0] = 0xB0u;
        input[1] = (uint8_t)i;
        input[2] = CP_IN_CC7_ABS;
        input[3] = CP_PAIR_SLOT_NONE;
        input[4] = CTRL_TYPE_PITCH;
        input[5] = CTRL_ID_CH1_VOLUME;
        wr_u16(input + 6, CP_IN_FLAG_REPLAY);
        memset(input + 12, 0xFF, 4u);
    }

    memcpy(blob, CP_MAGIC, 4u);
    wr_u16(blob + 4, CP_VERSION);
    wr_u16(blob + 6, CP_HEADER_SIZE);
    wr_u32(blob + 8, (uint32_t)total);
    wr_u16(blob + 16, 0x1234u);
    wr_u16(blob + 18, 0x5678u);
    wr_u16(blob + 24, LARGE_REPLAY_COUNT);
    blob[29] = 2u;
    wr_u32(blob + 12, cp_crc32(blob + 16, total - 16u));
    return total;
}

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
        .cable = 0u,
        .cin = (uint8_t)(status >> 4u),
        .len = 3u,
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

    uint8_t blob[CP_HEADER_SIZE + CP_INPUT_ENTRY_SIZE + CP_OUTPUT_ENTRY_SIZE];
    const size_t len = build_profile(blob);
    CHECK(!controller_profile_runtime_activate(blob, len, 0x1234u, 0x0001u));
    CHECK(controller_profile_runtime_activate(blob, len, 0x1234u, 0x5678u));
    CHECK(controller_profile_runtime_active());

    usb_midi_message_t message = midi(0x90u, 0x0Bu, 0x7Fu);
    CHECK(!controller_runtime_handle_midi(&message));
    CHECK(controller_runtime_pending_count() == 0u);

    message = midi(0xB0u, 0x55u, 100u);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(controller_runtime_dispatch_pending(4u) == 1u);
    CHECK(s_event_count == 1u);
    CHECK(s_events[0].type == CTRL_TYPE_PITCH);
    CHECK(s_events[0].id == CTRL_ID_CH1_VOLUME);
    CHECK(s_events[0].value == 100);

    controller_runtime_set_connected(true);
    CHECK(controller_runtime_dispatch_pending(4u) == 1u);
    CHECK(s_event_count == 2u);
    CHECK(s_events[1].id == CTRL_ID_CH1_VOLUME);
    CHECK(s_events[1].value == 100);

    uint8_t packet[4] = {0};
    CHECK(controller_profile_runtime_map_led(1u, 0u, 1u, packet));
    CHECK(packet[0] == 0x09u);
    CHECK(packet[1] == 0x90u);
    CHECK(packet[2] == 0x0Bu);
    CHECK(packet[3] == 0x7Fu);

    controller_runtime_diagnostics_t diagnostics;
    controller_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.dynamic_profile_active);
    CHECK(diagnostics.midi_messages == 2u);
    CHECK(diagnostics.mapped_messages == 1u);
    CHECK(diagnostics.non_emitting_messages == 1u);
    CHECK(diagnostics.reconnect_snapshots == 1u);

    controller_profile_runtime_clear();
    CHECK(!controller_profile_runtime_active());
    message = midi(0x90u, 0x0Bu, 0x7Fu);
    CHECK(controller_runtime_handle_midi(&message));
    CHECK(controller_runtime_dispatch_pending(1u) == 1u);
    CHECK(s_events[2].id == CTRL_ID_DECK1_PLAY);

    CHECK(controller_runtime_init(&config) == ESP_OK);
    uint8_t large_blob[CP_HEADER_SIZE +
                       LARGE_REPLAY_COUNT * CP_INPUT_ENTRY_SIZE];
    const size_t large_len = build_large_replay_profile(large_blob);
    CHECK(controller_profile_runtime_activate(large_blob, large_len,
                                              0x1234u, 0x5678u));
    s_event_count = 0u;
    for (size_t i = 0u; i < LARGE_REPLAY_COUNT; ++i) {
        message = midi(0xB0u, (uint8_t)i, (uint8_t)i);
        CHECK(controller_runtime_handle_midi(&message));
        CHECK(controller_runtime_dispatch_pending(1u) == 1u);
    }
    s_event_count = 0u;
    controller_runtime_set_connected(true);
    size_t snapshot_dispatched = 0u;
    while (snapshot_dispatched < LARGE_REPLAY_COUNT) {
        const size_t batch = controller_runtime_dispatch_pending(7u);
        CHECK(batch > 0u && batch <= 7u);
        snapshot_dispatched += batch;
    }
    CHECK(snapshot_dispatched == LARGE_REPLAY_COUNT);
    CHECK(s_event_count == LARGE_REPLAY_COUNT);
    for (size_t i = 0u; i < LARGE_REPLAY_COUNT; ++i) {
        CHECK(s_events[i].value == (int16_t)i);
    }
    CHECK(controller_runtime_pending_count() == 0u);

    printf("PASS P4 local compiled controller profile runtime\n");
    printf("CHECKS=%u EVENTS=%zu\n", s_checks, s_event_count);
    return 0;
}
