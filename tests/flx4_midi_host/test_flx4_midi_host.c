#include "flx4_midi_host.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

size_t flx4_midi_host_midi_out_queue_capacity(void);

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

static void test_connected_state_can_be_refreshed_after_edge_publication(void)
{
    flx4_midi_host_test_connection_event_t ev = { 0 };

    flx4_midi_host_test_reset_connection_state();
    assert(!flx4_midi_host_test_publish_connection_refresh(&ev));

    assert(flx4_midi_host_test_publish_connection_state(true, &ev));
    assert(!flx4_midi_host_test_publish_connection_state(true, &ev));

    memset(&ev, 0, sizeof(ev));
    assert(flx4_midi_host_test_publish_connection_refresh(&ev));
    assert(ev.type == 0x82);
    assert(ev.id == 0x70);
    assert(ev.value == 1);

    assert(flx4_midi_host_test_publish_connection_state(false, &ev));
    assert(!flx4_midi_host_test_publish_connection_refresh(&ev));
}

static void test_vu_meter_packets_are_low_priority(void)
{
    const uint8_t d1_vu[4] = { 0x0B, 0xB0, 0x02, 0x40 };
    const uint8_t d2_vu[4] = { 0x0B, 0xB1, 0x02, 0x7F };
    const uint8_t play_led[4] = { 0x09, 0x90, 0x0B, 0x7F };
    const uint8_t non_vu_cc[4] = { 0x0B, 0xB0, 0x03, 0x40 };

    assert(flx4_midi_host_is_vu_meter_packet(d1_vu));
    assert(flx4_midi_host_is_vu_meter_packet(d2_vu));
    assert(!flx4_midi_host_is_vu_meter_packet(play_led));
    assert(!flx4_midi_host_is_vu_meter_packet(non_vu_cc));
}

static void test_vu_meter_packets_drop_when_out_queue_has_backlog(void)
{
    const uint8_t d1_vu[4] = { 0x0B, 0xB0, 0x02, 0x40 };
    const uint8_t play_led[4] = { 0x09, 0x90, 0x0B, 0x7F };

    assert(!flx4_midi_host_should_drop_out_packet(d1_vu, 32, 32));
    assert(flx4_midi_host_should_drop_out_packet(d1_vu, 31, 32));
    assert(flx4_midi_host_should_drop_out_packet(d1_vu, 0, 32));

    assert(!flx4_midi_host_should_drop_out_packet(play_led, 0, 32));
    assert(!flx4_midi_host_should_drop_out_packet(NULL, 0, 32));
    assert(!flx4_midi_host_should_drop_out_packet(d1_vu, 32, 0));
}

static void test_midi_out_queue_capacity_covers_phase7_forced_snapshot(void)
{
    const size_t deck_transport_leds = 4; // Play, Cue, PFL, Sync
    const size_t deck_pad_mode_leds = 8;
    const size_t deck_loop_leds = 2;
    const size_t deck_beat_loop_pad_leds = 8;
    const size_t forced_snapshot_packets =
        2 * (deck_transport_leds + deck_pad_mode_leds +
             deck_loop_leds + deck_beat_loop_pad_leds);

    assert(flx4_midi_host_midi_out_queue_capacity() >= forced_snapshot_packets);
}

static void test_descriptor_hex_row_formatter_is_deterministic(void)
{
    const uint8_t bytes[] = {
        0x09, 0x02, 0x4B, 0x00, 0x03, 0x01, 0x00, 0x80,
        0xFA, 0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01,
        0x00, 0x00,
    };
    char line[16u * 3u + 1u] = { 0 };

    assert(flx4_midi_format_descriptor_hex_row(bytes, sizeof(bytes), 0, line, sizeof(line)));
    assert(strcmp(line, "09 02 4B 00 03 01 00 80 FA 09 04 00 00 00 01 01") == 0);

    memset(line, 0, sizeof(line));
    assert(flx4_midi_format_descriptor_hex_row(bytes, sizeof(bytes), 16, line, sizeof(line)));
    assert(strcmp(line, "00 00") == 0);

    assert(!flx4_midi_format_descriptor_hex_row(bytes, sizeof(bytes), sizeof(bytes), line, sizeof(line)));
    assert(!flx4_midi_format_descriptor_hex_row(NULL, sizeof(bytes), 0, line, sizeof(line)));
    assert(!flx4_midi_format_descriptor_hex_row(bytes, sizeof(bytes), 0, NULL, sizeof(line)));
    assert(!flx4_midi_format_descriptor_hex_row(bytes, sizeof(bytes), 0, line, 4));
}

int main(void)
{
    test_parse_note_on_packet();
    test_parse_control_change_packet();
    test_rejects_reserved_or_null_arguments();
    test_finds_midi_streaming_in_endpoint();
    test_rejects_truncated_descriptor();
    test_connection_state_publications_are_edge_triggered();
    test_connected_state_can_be_refreshed_after_edge_publication();
    test_vu_meter_packets_are_low_priority();
    test_vu_meter_packets_drop_when_out_queue_has_backlog();
    test_midi_out_queue_capacity_covers_phase7_forced_snapshot();
    test_descriptor_hex_row_formatter_is_deterministic();
    puts("flx4_midi_host tests passed");
    return 0;
}
