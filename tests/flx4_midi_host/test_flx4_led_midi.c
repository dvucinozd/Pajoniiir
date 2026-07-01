#include "flx4_led_midi.h"
#include "control_link.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_packet(uint8_t led,
                          uint8_t state,
                          uint8_t deck,
                          uint8_t cin,
                          uint8_t status,
                          uint8_t data1,
                          uint8_t data2)
{
    uint8_t packet[4] = { 0 };

    assert(flx4_led_midi_build_packet(led, state, deck, packet));
    assert(packet[0] == cin);
    assert(packet[1] == status);
    assert(packet[2] == data1);
    assert(packet[3] == data2);
}

static void test_transport_and_mode_led_packets(void)
{
    expect_packet(LED_PLAY, 1, CTRL_DECK_1, 0x09, 0x90, 0x0B, 0x7F);
    expect_packet(LED_CUE, 0, CTRL_DECK_2, 0x09, 0x91, 0x0C, 0x00);
    expect_packet(LED_PFL, 1, CTRL_DECK_2, 0x09, 0x91, 0x54, 0x7F);
    expect_packet(LED_SYNC, 1, CTRL_DECK_1, 0x09, 0x90, 0x58, 0x7F);
    expect_packet(LED_SYNC, 0, CTRL_DECK_2, 0x09, 0x91, 0x58, 0x00);
    expect_packet(LED_LOOP_IN, 1, CTRL_DECK_1, 0x09, 0x90, 0x10, 0x7F);
    expect_packet(LED_LOOP_IN, 0, CTRL_DECK_2, 0x09, 0x91, 0x10, 0x00);
    expect_packet(LED_LOOP_OUT, 1, CTRL_DECK_1, 0x09, 0x90, 0x11, 0x7F);
    expect_packet(LED_LOOP_OUT, 0, CTRL_DECK_2, 0x09, 0x91, 0x11, 0x00);

    expect_packet(LED_PAD_MODE_HOT_CUE, 1, CTRL_DECK_1, 0x09, 0x90, 0x1B, 0x7F);
    expect_packet(LED_PAD_MODE_KEYBOARD, 1, CTRL_DECK_2, 0x09, 0x91, 0x69, 0x7F);
    expect_packet(LED_PAD_MODE_PAD_FX1, 0, CTRL_DECK_1, 0x09, 0x90, 0x1E, 0x00);
    expect_packet(LED_PAD_MODE_PAD_FX2, 1, CTRL_DECK_2, 0x09, 0x91, 0x6B, 0x7F);
    expect_packet(LED_PAD_MODE_BEAT_JUMP, 1, CTRL_DECK_1, 0x09, 0x90, 0x20, 0x7F);
    expect_packet(LED_PAD_MODE_BEAT_LOOP, 1, CTRL_DECK_2, 0x09, 0x91, 0x6D, 0x7F);
    expect_packet(LED_PAD_MODE_SAMPLER, 1, CTRL_DECK_1, 0x09, 0x90, 0x22, 0x7F);
    expect_packet(LED_PAD_MODE_KEY_SHIFT, 1, CTRL_DECK_2, 0x09, 0x91, 0x6F, 0x7F);
    expect_packet(LED_SMART_CFX, 1, CTRL_DECK_1, 0x09, 0x96, 0x00, 0x7F);
    expect_packet(LED_SMART_CFX, 0, CTRL_DECK_1, 0x09, 0x96, 0x00, 0x00);
    expect_packet(LED_SMART_FADER, 1, CTRL_DECK_1, 0x09, 0x96, 0x01, 0x7F);
    expect_packet(LED_SMART_FADER, 0, CTRL_DECK_1, 0x09, 0x96, 0x01, 0x00);
    expect_packet(LED_BEAT_FX_ON, 1, CTRL_DECK_1, 0x09, 0x94, 0x47, 0x7F);
    expect_packet(LED_BEAT_FX_ON, 0, CTRL_DECK_1, 0x09, 0x94, 0x47, 0x00);
    expect_packet(LED_BEAT_FX_ON, 1, CTRL_DECK_2, 0x09, 0x95, 0x47, 0x7F);
}

static void test_beat_loop_pad_led_packets(void)
{
    expect_packet(LED_BEAT_LOOP_PAD_1, 1, CTRL_DECK_1, 0x09, 0x97, 0x60, 0x7F);
    expect_packet(LED_BEAT_LOOP_PAD_8, 0, CTRL_DECK_1, 0x09, 0x97, 0x67, 0x00);
    expect_packet(LED_BEAT_LOOP_PAD_1, 1, CTRL_DECK_2, 0x09, 0x99, 0x60, 0x7F);
    expect_packet(LED_BEAT_LOOP_PAD_8, 0, CTRL_DECK_2, 0x09, 0x99, 0x67, 0x00);
}

static void test_pad_fx_pad_led_packets(void)
{
    expect_packet(LED_PAD_FX1_PAD_1, 1, CTRL_DECK_1, 0x09, 0x97, 0x10, 0x7F);
    expect_packet(LED_PAD_FX1_PAD_8, 0, CTRL_DECK_1, 0x09, 0x97, 0x17, 0x00);
    expect_packet(LED_PAD_FX1_PAD_1, 1, CTRL_DECK_2, 0x09, 0x99, 0x10, 0x7F);
    expect_packet(LED_PAD_FX1_PAD_8, 0, CTRL_DECK_2, 0x09, 0x99, 0x17, 0x00);

    expect_packet(LED_PAD_FX2_PAD_1, 1, CTRL_DECK_1, 0x09, 0x97, 0x50, 0x7F);
    expect_packet(LED_PAD_FX2_PAD_8, 0, CTRL_DECK_1, 0x09, 0x97, 0x57, 0x00);
    expect_packet(LED_PAD_FX2_PAD_1, 1, CTRL_DECK_2, 0x09, 0x99, 0x50, 0x7F);
    expect_packet(LED_PAD_FX2_PAD_8, 0, CTRL_DECK_2, 0x09, 0x99, 0x57, 0x00);
}

static void test_vu_and_rejects(void)
{
    expect_packet(LED_VU_METER, 0x40, CTRL_DECK_1, 0x0B, 0xB0, 0x02, 0x40);
    expect_packet(LED_VU_METER, 0xFF, CTRL_DECK_2, 0x0B, 0xB1, 0x02, 0x7F);

    uint8_t packet[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
    assert(!flx4_led_midi_build_packet(LED_PLAY, 1, CTRL_DECK_NONE, packet));
    assert(!flx4_led_midi_build_packet(0xFF, 1, CTRL_DECK_1, packet));
    assert(!flx4_led_midi_build_packet(LED_PLAY, 1, CTRL_DECK_1, NULL));
    assert(memcmp(packet, ((uint8_t[4]) { 0xAA, 0xAA, 0xAA, 0xAA }), 4) == 0);
}

int main(void)
{
    test_transport_and_mode_led_packets();
    test_beat_loop_pad_led_packets();
    test_pad_fx_pad_led_packets();
    test_vu_and_rejects();
    puts("flx4_led_midi tests passed");
    return 0;
}
