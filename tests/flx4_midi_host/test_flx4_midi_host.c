#include "flx4_midi_host.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void)
{
    test_parse_note_on_packet();
    test_parse_control_change_packet();
    test_rejects_reserved_or_null_arguments();
    puts("flx4_midi_host tests passed");
    return 0;
}
