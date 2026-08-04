#pragma once

#include <stdbool.h>
#include <stdint.h>

bool flx4_led_midi_build_packet(uint8_t led,
                                uint8_t state,
                                uint8_t deck,
                                uint8_t packet[4]);
bool flx4_led_midi_builtin_authoritative(uint8_t led);
