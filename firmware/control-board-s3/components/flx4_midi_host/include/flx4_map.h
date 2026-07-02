#pragma once

#include "flx4_midi_host.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
    flx4_14bit_state_t headphone_level;
    flx4_14bit_state_t master_volume;
    flx4_14bit_state_t crossfader;
    uint8_t beat_fx_depth;
    bool beat_fx_depth_valid;
    bool beat_fx_target_ch1;
    bool beat_fx_target_ch2;
} flx4_map_state_t;

typedef bool (*flx4_map_snapshot_emit_cb_t)(uint8_t type,
                                            uint8_t id,
                                            int16_t value,
                                            void *ctx);

void flx4_map_init(flx4_map_state_t *state);
bool flx4_map_message(flx4_map_state_t *state,
                      const flx4_midi_message_t *msg,
                      flx4_control_event_t *out);
size_t flx4_map_emit_snapshot(const flx4_map_state_t *state,
                              flx4_map_snapshot_emit_cb_t cb,
                              void *ctx);
