#include "flx4_midi_host.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_parse_note_on_packet(void)
{
    const uint8_t packet[4] = { 0x09, 0x90, 0x0B, 0x7F };
    flx4_midi_message_t msg = { 0 };

    assert(flx4_midi_parse_usb_packet(packet, &msg));
    assert(msg.cable == 0);
    assert(msg.cin == FLX4_USB_MIDI_CIN_NOTE_ON);
    assert(msg.len == 3);
    assert(msg.status == 0x90);
    assert(msg.data1 == 0x0B);
    assert(msg.data2 == 0x7F);
}

static void test_parse_control_change_packet(void)
{
    const uint8_t packet[4] = { 0x0B, 0xB6, 0x1F, 0x40 };
    flx4_midi_message_t msg = { 0 };

    assert(flx4_midi_parse_usb_packet(packet, &msg));
    assert(msg.cable == 0);
    assert(msg.cin == FLX4_USB_MIDI_CIN_CONTROL_CHANGE);
    assert(msg.len == 3);
    assert(msg.status == 0xB6);
    assert(msg.data1 == 0x1F);
    assert(msg.data2 == 0x40);
}

static void test_rejects_reserved_or_null_arguments(void)
{
    const uint8_t reserved_packet[4] = { 0x00, 0x00, 0x00, 0x00 };
    flx4_midi_message_t msg = { 0 };

    assert(!flx4_midi_parse_usb_packet(NULL, &msg));
    assert(!flx4_midi_parse_usb_packet(reserved_packet, NULL));
    assert(!flx4_midi_parse_usb_packet(reserved_packet, &msg));
}

static void test_finds_midi_streaming_in_endpoint(void)
{
    const uint8_t cfg[] = {
        9, 2, 34, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 0, 0x01, 0x01, 0x00, 0,
        9, 4, 1, 0, 1, 0x01, 0x03, 0x00, 0,
        7, 5, 0x81, 0x02, 64, 0, 0,
    };
    uint8_t interface_num = 0xFF;
    uint8_t alternate_setting = 0xFF;
    uint8_t in_ep_addr = 0;
    uint16_t in_ep_mps = 0;

    assert(flx4_midi_find_streaming_in_endpoint(cfg, sizeof(cfg),
                                                &interface_num,
                                                &alternate_setting,
                                                &in_ep_addr,
                                                &in_ep_mps));
    assert(interface_num == 1);
    assert(alternate_setting == 0);
    assert(in_ep_addr == 0x81);
    assert(in_ep_mps == 64);
}

static void test_rejects_truncated_descriptor(void)
{
    const uint8_t truncated_cfg[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 1, 0, 1, 0x01, 0x03, 0x00, 0,
        7, 5, 0x81, 0x02, 64,
    };
    uint8_t interface_num = 0;
    uint8_t alternate_setting = 0;
    uint8_t in_ep_addr = 0;
    uint16_t in_ep_mps = 0;

    assert(!flx4_midi_find_streaming_in_endpoint(truncated_cfg, sizeof(truncated_cfg),
                                                 &interface_num,
                                                 &alternate_setting,
                                                 &in_ep_addr,
                                                 &in_ep_mps));
}

static void test_connection_state_publications_are_edge_triggered(void)
{
    flx4_midi_host_test_connection_event_t ev = { 0 };

    flx4_midi_host_test_reset_connection_state();

    assert(!flx4_midi_host_test_publish_connection_state(false, &ev));

    assert(flx4_midi_host_test_publish_connection_state(true, &ev));
    assert(ev.type == 0x82);
    assert(ev.id == 0x70);
    assert(ev.value == 1);

    assert(!flx4_midi_host_test_publish_connection_state(true, &ev));

    assert(flx4_midi_host_test_publish_connection_state(false, &ev));
    assert(ev.type == 0x82);
    assert(ev.id == 0x70);
    assert(ev.value == 0);

    assert(!flx4_midi_host_test_publish_connection_state(false, &ev));
    assert(!flx4_midi_host_test_publish_connection_state(true, NULL));
}

int main(void)
{
    test_parse_note_on_packet();
    test_parse_control_change_packet();
    test_rejects_reserved_or_null_arguments();
    test_finds_midi_streaming_in_endpoint();
    test_rejects_truncated_descriptor();
    test_connection_state_publications_are_edge_triggered();
    puts("flx4_midi_host tests passed");
    return 0;
}
