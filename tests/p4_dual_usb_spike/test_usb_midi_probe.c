#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usb_midi_probe.h"

static unsigned s_tests_run;

#define CHECK(expr) do { \
    s_tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static const uint8_t s_valid_bulk_config[] = {
    9, 2, 41, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 0xFF, 0, 0, 0,
    9, 4, 3, 1, 2, 0x01, 0x03, 0, 0,
    7, 5, 0x82, 0x02, 64, 0, 0,
    7, 5, 0x02, 0x02, 64, 0, 0,
};

static const uint8_t s_valid_interrupt_config[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 7, 0, 2, 0x01, 0x03, 0, 0,
    7, 5, 0x81, 0x03, 16, 0, 1,
    7, 5, 0x01, 0x03, 16, 0, 1,
};

static void test_valid_bulk(void)
{
    usb_midi_endpoints_t endpoints;
    CHECK(usb_midi_find_streaming_endpoints(s_valid_bulk_config,
                                            sizeof(s_valid_bulk_config),
                                            &endpoints));
    CHECK(endpoints.interface_num == 3u);
    CHECK(endpoints.alternate_setting == 1u);
    CHECK(endpoints.in_ep_addr == 0x82u);
    CHECK(endpoints.in_ep_mps == 64u);
    CHECK(endpoints.out_ep_addr == 0x02u);
    CHECK(endpoints.out_ep_mps == 64u);
    CHECK(usb_midi_config_has_interface_class(s_valid_bulk_config,
                                              sizeof(s_valid_bulk_config),
                                              0x08u,
                                              0xFFu) == false);
    CHECK(usb_midi_config_has_interface_class(s_valid_bulk_config,
                                              sizeof(s_valid_bulk_config),
                                              0x01u,
                                              0x03u));
}

static void test_valid_interrupt(void)
{
    usb_midi_endpoints_t endpoints;
    CHECK(usb_midi_find_streaming_endpoints(s_valid_interrupt_config,
                                            sizeof(s_valid_interrupt_config),
                                            &endpoints));
    CHECK(endpoints.interface_num == 7u);
    CHECK(endpoints.in_ep_addr == 0x81u);
    CHECK(endpoints.out_ep_addr == 0x01u);
    CHECK(endpoints.in_ep_mps == 16u);
    CHECK(endpoints.out_ep_mps == 16u);
}

static void test_descriptor_rejection(void)
{
    usb_midi_endpoints_t endpoints;
    uint8_t truncated[sizeof(s_valid_bulk_config)];
    memcpy(truncated, s_valid_bulk_config, sizeof(truncated));
    truncated[2] = 42u;
    CHECK(!usb_midi_find_streaming_endpoints(truncated,
                                             sizeof(truncated),
                                             &endpoints));

    uint8_t zero_len[sizeof(s_valid_bulk_config)];
    memcpy(zero_len, s_valid_bulk_config, sizeof(zero_len));
    zero_len[9] = 0u;
    CHECK(!usb_midi_find_streaming_endpoints(zero_len,
                                             sizeof(zero_len),
                                             &endpoints));

    uint8_t no_out[sizeof(s_valid_bulk_config)];
    memcpy(no_out, s_valid_bulk_config, sizeof(no_out));
    no_out[36] = 0x83u;
    CHECK(!usb_midi_find_streaming_endpoints(no_out,
                                             sizeof(no_out),
                                             &endpoints));

    uint8_t wrong_class[sizeof(s_valid_bulk_config)];
    memcpy(wrong_class, s_valid_bulk_config, sizeof(wrong_class));
    wrong_class[23] = 0xFFu;
    CHECK(!usb_midi_find_streaming_endpoints(wrong_class,
                                             sizeof(wrong_class),
                                             &endpoints));

    CHECK(!usb_midi_find_streaming_endpoints(NULL, 0u, &endpoints));
    CHECK(!usb_midi_find_streaming_endpoints(s_valid_bulk_config,
                                             sizeof(s_valid_bulk_config),
                                             NULL));
}

static void test_packets(void)
{
    usb_midi_message_t message;
    const uint8_t note_on[4] = { 0x09, 0x90, 0x0B, 0x7F };
    CHECK(usb_midi_parse_event_packet(note_on, &message));
    CHECK(message.cable == 0u);
    CHECK(message.cin == 0x09u);
    CHECK(message.len == 3u);
    CHECK(message.status == 0x90u);
    CHECK(message.data1 == 0x0Bu);
    CHECK(message.data2 == 0x7Fu);

    const uint8_t program_change[4] = { 0x2C, 0xC1, 0x05, 0x00 };
    CHECK(usb_midi_parse_event_packet(program_change, &message));
    CHECK(message.cable == 2u);
    CHECK(message.len == 2u);

    const uint8_t sysex_end_one[4] = { 0x05, 0xF7, 0x00, 0x00 };
    CHECK(usb_midi_parse_event_packet(sysex_end_one, &message));
    CHECK(message.len == 1u);

    const uint8_t reserved[4] = { 0x00, 0x00, 0x00, 0x00 };
    CHECK(!usb_midi_parse_event_packet(reserved, &message));
    CHECK(!usb_midi_parse_event_packet(NULL, &message));
    CHECK(!usb_midi_parse_event_packet(note_on, NULL));
}

int main(void)
{
    test_valid_bulk();
    test_valid_interrupt();
    test_descriptor_rejection();
    test_packets();
    printf("PASS p4 dual USB MIDI probe parser\n");
    printf("TESTS_RUN=%u\n", s_tests_run);
    return 0;
}
