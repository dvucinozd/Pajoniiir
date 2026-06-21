#include "flx4_led_midi.h"

#include "control_link.h"

static bool note_for_led(uint8_t led, uint8_t *note)
{
    if (!note) {
        return false;
    }

    switch (led) {
    case LED_PLAY:
        *note = 0x0B;
        return true;
    case LED_CUE:
        *note = 0x0C;
        return true;
    case LED_PFL:
        *note = 0x54;
        return true;
    case LED_SYNC:
        *note = 0x58;
        return true;
    case LED_PAD_MODE_HOT_CUE:
        *note = 0x1B;
        return true;
    case LED_PAD_MODE_KEYBOARD:
        *note = 0x69;
        return true;
    case LED_PAD_MODE_PAD_FX1:
        *note = 0x1E;
        return true;
    case LED_PAD_MODE_PAD_FX2:
        *note = 0x6B;
        return true;
    case LED_PAD_MODE_BEAT_JUMP:
        *note = 0x20;
        return true;
    case LED_PAD_MODE_BEAT_LOOP:
        *note = 0x6D;
        return true;
    case LED_PAD_MODE_SAMPLER:
        *note = 0x22;
        return true;
    case LED_PAD_MODE_KEY_SHIFT:
        *note = 0x6F;
        return true;
    default:
        return false;
    }
}

bool flx4_led_midi_build_packet(uint8_t led,
                                uint8_t state,
                                uint8_t deck,
                                uint8_t packet[4])
{
    if (!packet || (deck != CTRL_DECK_1 && deck != CTRL_DECK_2)) {
        return false;
    }

    if (led == LED_VU_METER) {
        packet[0] = 0x0B;
        packet[1] = (deck == CTRL_DECK_1) ? 0xB0 : 0xB1;
        packet[2] = 0x02;
        packet[3] = (uint8_t)(state & 0x7F);
        return true;
    }

    uint8_t note = 0;
    if (!note_for_led(led, &note)) {
        return false;
    }

    packet[0] = 0x09;
    packet[1] = (deck == CTRL_DECK_1) ? 0x90 : 0x91;
    packet[2] = note;
    packet[3] = (state != 0) ? 0x7F : 0x00;
    return true;
}
