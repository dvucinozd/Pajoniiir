#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller_led_runtime.h"
#include "controller_profile.h"
#include "controller_profile_runtime.h"
#include "control_link.h"

static unsigned s_checks;
static esp_err_t s_send_result = ESP_OK;
static uint8_t s_last_sent[4];
static uint32_t s_send_calls;

#define CHECK(expr) do { \
    s_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

esp_err_t controller_usb_host_send_packet(const uint8_t packet[4])
{
    s_send_calls++;
    memcpy(s_last_sent, packet, sizeof(s_last_sent));
    return s_send_result;
}

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

static void write_output(uint8_t *entry, uint8_t led, uint8_t status,
                         uint8_t data1, uint8_t off_value,
                         uint8_t on_value, uint8_t blink_value)
{
    entry[0] = led;
    entry[1] = CP_DECK_ANY;
    entry[2] = CP_OUT_NOTE_ONOFF;
    entry[3] = status;
    entry[4] = data1;
    entry[5] = off_value;
    entry[6] = on_value;
    entry[7] = blink_value;
}

static size_t build_profile(uint8_t blob[CP_HEADER_SIZE +
                                         2u * CP_OUTPUT_ENTRY_SIZE])
{
    const size_t total = CP_HEADER_SIZE + 2u * CP_OUTPUT_ENTRY_SIZE;
    memset(blob, 0, total);
    memcpy(blob, CP_MAGIC, 4u);
    wr_u16(blob + 4, CP_VERSION);
    wr_u16(blob + 6, CP_HEADER_SIZE);
    wr_u32(blob + 8, (uint32_t)total);
    wr_u16(blob + 16, 0x1234u);
    wr_u16(blob + 18, 0x5678u);
    wr_u32(blob + 20, CP_PF_LED_FEEDBACK);
    wr_u16(blob + 24, 0u);
    wr_u16(blob + 26, 2u);
    blob[28] = 0u;
    blob[29] = 2u;

    write_output(blob + CP_HEADER_SIZE,
                 LED_CUE, 0x92u, 0x33u, 0x11u, 0x66u, 0x44u);
    write_output(blob + CP_HEADER_SIZE + CP_OUTPUT_ENTRY_SIZE,
                 LED_TRACK_LOAD_DECK1, 0x92u, 0x55u,
                 0x01u, 0x02u, 0x03u);
    wr_u32(blob + 12, cp_crc32(blob + 16, total - 16u));
    return total;
}

int main(void)
{
    controller_profile_runtime_init();

    uint8_t packet[4] = {0};
    CHECK(controller_led_runtime_build_packet(
        LED_PLAY, 1u, CTRL_DECK_1, packet));
    CHECK(packet[0] == 0x09u);
    CHECK(packet[1] == 0x90u);
    CHECK(packet[2] == 0x0Bu);
    CHECK(packet[3] == 0x7Fu);

    uint8_t blob[CP_HEADER_SIZE + 2u * CP_OUTPUT_ENTRY_SIZE];
    const size_t len = build_profile(blob);
    CHECK(controller_profile_runtime_activate(blob, len,
                                              0x1234u, 0x5678u));

    CHECK(controller_led_runtime_build_packet(
        LED_CUE, 1u, CTRL_DECK_1, packet));
    CHECK(packet[0] == 0x09u);
    CHECK(packet[1] == 0x92u);
    CHECK(packet[2] == 0x33u);
    CHECK(packet[3] == 0x66u);

    CHECK(controller_led_runtime_build_packet(
        LED_PLAY, 1u, CTRL_DECK_1, packet));
    CHECK(packet[1] == 0x90u);
    CHECK(packet[2] == 0x0Bu);
    CHECK(packet[3] == 0x7Fu);

    CHECK(controller_led_runtime_build_packet(
        LED_TRACK_LOAD_DECK1, 1u, CTRL_DECK_1, packet));
    CHECK(packet[1] == 0x9Fu);
    CHECK(packet[2] == 0x00u);
    CHECK(packet[3] == 0x7Fu);

    CHECK(controller_led_runtime_build_packet(
        LED_VU_METER, 63u, CTRL_DECK_2, packet));
    CHECK(packet[0] == 0x0Bu);
    CHECK(packet[1] == 0xB1u);
    CHECK(packet[2] == 0x02u);
    CHECK(packet[3] == 63u);

    CHECK(!controller_led_runtime_build_packet(
        0xFFu, 1u, CTRL_DECK_1, packet));
    CHECK(!controller_led_runtime_build_packet(
        LED_PLAY, 1u, 0xFFu, packet));

    s_send_result = ESP_OK;
    CHECK(controller_led_runtime_send(
        LED_CUE, 2u, CTRL_DECK_1) == ESP_OK);
    CHECK(s_send_calls == 1u);
    CHECK(s_last_sent[1] == 0x92u);
    CHECK(s_last_sent[3] == 0x44u);

    s_send_result = ESP_ERR_TIMEOUT;
    CHECK(controller_led_runtime_send(
        LED_PLAY, 1u, CTRL_DECK_1) == ESP_ERR_TIMEOUT);
    CHECK(s_send_calls == 2u);

    controller_led_runtime_diagnostics_t diagnostics;
    controller_led_runtime_get_diagnostics(&diagnostics);
    CHECK(diagnostics.dynamic_packets == 2u);
    CHECK(diagnostics.builtin_packets == 5u);
    CHECK(diagnostics.builtin_fallbacks == 3u);
    CHECK(diagnostics.unsupported == 2u);
    CHECK(diagnostics.send_failures == 1u);

    printf("PASS P4 local controller LED runtime\n");
    printf("CHECKS=%u SENDS=%u\n", s_checks, s_send_calls);
    return 0;
}
