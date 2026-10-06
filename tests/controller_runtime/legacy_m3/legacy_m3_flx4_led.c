/* Test-only reference from Pajoniiir-M3 e95417c4e2fea007d2c1dcb693790914692c62ba.
 * Original logic is preserved; public symbols are namespaced for linking. */
#include "legacy_m3_flx4_map.h"
#include "control_link.h"

static bool note_for_led(uint8_t led, uint8_t *note)
{
    if (!note) {
        return false;
    }

    if (led >= LED_BEAT_LOOP_PAD_1 && led <= LED_BEAT_LOOP_PAD_8) {
        *note = (uint8_t)(0x60u + (led - LED_BEAT_LOOP_PAD_1));
        return true;
    }
    if (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_8) {
        *note = (uint8_t)(0x20u + (led - LED_BEAT_JUMP_PAD_1));
        return true;
    }
    if (led >= LED_BEAT_JUMP_SHIFT_HELPER_7 && led <= LED_BEAT_JUMP_SHIFT_HELPER_8) {
        *note = (uint8_t)(0x26u + (led - LED_BEAT_JUMP_SHIFT_HELPER_7));
        return true;
    }
    if (led >= LED_HOT_CUE_PAD_1 && led <= LED_HOT_CUE_PAD_8) {
        *note = (uint8_t)(0x00u + (led - LED_HOT_CUE_PAD_1));
        return true;
    }
    if (led >= LED_PAD_FX1_PAD_1 && led <= LED_PAD_FX1_PAD_8) {
        *note = (uint8_t)(0x10u + (led - LED_PAD_FX1_PAD_1));
        return true;
    }
    if (led >= LED_PAD_FX2_PAD_1 && led <= LED_PAD_FX2_PAD_8) {
        *note = (uint8_t)(0x50u + (led - LED_PAD_FX2_PAD_1));
        return true;
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
    case LED_LOOP_IN:
        *note = 0x10;
        return true;
    case LED_LOOP_OUT:
        *note = 0x11;
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
    case LED_SMART_CFX:
        *note = 0x00;
        return true;
    case LED_SMART_FADER:
        *note = 0x01;
        return true;
    case LED_BEAT_FX_ON:
        *note = 0x47;
        return true;
    case LED_MASTER_CUE:
        *note = 0x63;
        return true;
    case LED_CENSOR:
        *note = 0x0E;
        return true;
    case LED_CUE_SHIFT:
        *note = 0x48;
        return true;
    case LED_LOOP_ADJUST_IN:
        *note = 0x4C;
        return true;
    case LED_LOOP_ADJUST_OUT:
        *note = 0x4E;
        return true;
    case LED_TRACK_LOAD_DECK1:
        *note = 0x00;
        return true;
    case LED_TRACK_LOAD_DECK2:
        *note = 0x01;
        return true;
    default:
        return false;
    }
}

bool legacy_m3_flx4_led_midi_build_packet(uint8_t led,
                                uint8_t state,
                                uint8_t deck,
                                uint8_t packet[4])
{
    if (!packet) {
        return false;
    }
    if (deck != CTRL_DECK_1 && deck != CTRL_DECK_2) {
        return false;
    }
    const bool is_d2 = (deck == CTRL_DECK_2);

    if (led == LED_VU_METER) {
        packet[0] = 0x0B;
        packet[1] = is_d2 ? 0xB1 : 0xB0;
        packet[2] = 0x02;
        packet[3] = (uint8_t)(state & 0x7F);
        return true;
    }

    uint8_t note = 0;
    if (!note_for_led(led, &note)) {
        return false;
    }

    packet[0] = 0x09;
    if ((led >= LED_BEAT_LOOP_PAD_1 && led <= LED_BEAT_LOOP_PAD_8) ||
        (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_8) ||
        (led >= LED_HOT_CUE_PAD_1 && led <= LED_HOT_CUE_PAD_8) ||
        (led >= LED_PAD_FX1_PAD_1 && led <= LED_PAD_FX1_PAD_8) ||
        (led >= LED_PAD_FX2_PAD_1 && led <= LED_PAD_FX2_PAD_8)) {
        packet[1] = is_d2 ? 0x99 : 0x97;
    } else if (led >= LED_BEAT_JUMP_SHIFT_HELPER_7 && led <= LED_BEAT_JUMP_SHIFT_HELPER_8) {
        packet[1] = is_d2 ? 0x9A : 0x98;
    } else if (led == LED_BEAT_FX_ON) {
        packet[1] = is_d2 ? 0x95 : 0x94;
    } else if (led == LED_TRACK_LOAD_DECK1 || led == LED_TRACK_LOAD_DECK2) {
        packet[1] = 0x9F;
    } else if (led == LED_SMART_CFX || led == LED_SMART_FADER || led == LED_MASTER_CUE) {
        packet[1] = 0x96;
    } else {
        packet[1] = is_d2 ? 0x91 : 0x90;
    }
    packet[2] = note;
    packet[3] = (state != 0) ? 0x7F : 0x00;
    return true;
}

bool legacy_m3_flx4_led_midi_build_shifted_mirror_packet(uint8_t led,
                                               uint8_t state,
                                               uint8_t deck,
                                               uint8_t packet[4])
{
    const bool is_mirrored_pad =
        (led >= LED_BEAT_LOOP_PAD_1 && led <= LED_BEAT_LOOP_PAD_8) ||
        (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_6) ||
        (led >= LED_HOT_CUE_PAD_1 && led <= LED_HOT_CUE_PAD_8) ||
        (led >= LED_PAD_FX1_PAD_1 && led <= LED_PAD_FX1_PAD_8) ||
        (led >= LED_PAD_FX2_PAD_1 && led <= LED_PAD_FX2_PAD_8);
    if (!is_mirrored_pad ||
        !legacy_m3_flx4_led_midi_build_packet(led, state, deck, packet)) {
        return false;
    }

    packet[1] = (deck == CTRL_DECK_2) ? 0x9A : 0x98;
    return true;
}
