/* Test-only reference from Pajoniiir-M3 e95417c4e2fea007d2c1dcb693790914692c62ba.
 * Original logic is preserved; public symbols are namespaced for linking. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "control_link.h"

typedef struct {
    uint8_t cable;
    uint8_t cin;
    uint8_t len;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} legacy_m3_flx4_midi_message_t;

typedef struct {
    int16_t msb;
    int16_t lsb;
    bool    msb_valid;
    bool    lsb_valid;
} legacy_m3_flx4_14bit_state_t;

typedef struct {
    legacy_m3_flx4_14bit_state_t tempo[2];
    legacy_m3_flx4_14bit_state_t channel_volume[2];
    legacy_m3_flx4_14bit_state_t trim[2];
    legacy_m3_flx4_14bit_state_t eq_high[2];
    legacy_m3_flx4_14bit_state_t eq_mid[2];
    legacy_m3_flx4_14bit_state_t eq_low[2];
    legacy_m3_flx4_14bit_state_t filter[2];
    legacy_m3_flx4_14bit_state_t master_volume;
    legacy_m3_flx4_14bit_state_t headphone_mix;
    legacy_m3_flx4_14bit_state_t headphone_level;
    legacy_m3_flx4_14bit_state_t crossfader;
    uint8_t            beat_fx_depth;
    bool               beat_fx_depth_valid;
    bool               beat_fx_channel_valid;
    uint8_t            beat_fx_channel;
} legacy_m3_flx4_map_state_t;

typedef struct {
    uint8_t type;
    uint8_t id;
    int16_t value;
} legacy_m3_flx4_control_event_t;

void legacy_m3_flx4_map_init(legacy_m3_flx4_map_state_t *state);
bool legacy_m3_flx4_map_translate_message(legacy_m3_flx4_map_state_t *state,
                                const legacy_m3_flx4_midi_message_t *msg,
                                legacy_m3_flx4_control_event_t *out);

bool legacy_m3_flx4_midi_parse_usb_packet(const uint8_t packet[4], legacy_m3_flx4_midi_message_t *out);
bool legacy_m3_flx4_led_midi_build_packet(uint8_t led, uint8_t state, uint8_t deck, uint8_t packet[4]);
bool legacy_m3_flx4_led_midi_build_shifted_mirror_packet(uint8_t led,
                                               uint8_t state,
                                               uint8_t deck,
                                               uint8_t packet[4]);
