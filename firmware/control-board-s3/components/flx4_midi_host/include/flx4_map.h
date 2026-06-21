#pragma once

#include "flx4_midi_host.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t type;
    uint8_t id;
    int16_t value;
} flx4_control_event_t;

typedef struct {
    uint8_t msb;
    uint8_t lsb;
    bool msb_valid;
    bool lsb_valid;
} flx4_14bit_state_t;

typedef struct {
    flx4_14bit_state_t tempo[2];
    flx4_14bit_state_t channel_volume[2];
    flx4_14bit_state_t trim[2];
    flx4_14bit_state_t eq_high[2];
    flx4_14bit_state_t eq_mid[2];
    flx4_14bit_state_t eq_low[2];
    flx4_14bit_state_t filter[2];
    flx4_14bit_state_t headphone_mix;
    flx4_14bit_state_t crossfader;
} flx4_map_state_t;

void flx4_map_init(flx4_map_state_t *state);
bool flx4_map_message(flx4_map_state_t *state,
                      const flx4_midi_message_t *msg,
                      flx4_control_event_t *out);
