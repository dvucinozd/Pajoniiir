#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if defined(FLX4_MIDI_HOST_PC_TEST)
#include "esp_err.h"
#else
#include "esp_err.h"
#endif

typedef enum {
    FLX4_USB_MIDI_CIN_MISC             = 0x0,
    FLX4_USB_MIDI_CIN_CABLE_EVENT      = 0x1,
    FLX4_USB_MIDI_CIN_2BYTE_SYSTEM     = 0x2,
    FLX4_USB_MIDI_CIN_3BYTE_SYSTEM     = 0x3,
    FLX4_USB_MIDI_CIN_SYSEX_START      = 0x4,
    FLX4_USB_MIDI_CIN_SYSEX_END_1      = 0x5,
    FLX4_USB_MIDI_CIN_SYSEX_END_2      = 0x6,
    FLX4_USB_MIDI_CIN_SYSEX_END_3      = 0x7,
    FLX4_USB_MIDI_CIN_NOTE_OFF         = 0x8,
    FLX4_USB_MIDI_CIN_NOTE_ON          = 0x9,
    FLX4_USB_MIDI_CIN_POLY_PRESSURE    = 0xA,
    FLX4_USB_MIDI_CIN_CONTROL_CHANGE   = 0xB,
    FLX4_USB_MIDI_CIN_PROGRAM_CHANGE   = 0xC,
    FLX4_USB_MIDI_CIN_CHANNEL_PRESSURE = 0xD,
    FLX4_USB_MIDI_CIN_PITCH_BEND       = 0xE,
    FLX4_USB_MIDI_CIN_SINGLE_BYTE      = 0xF,
} flx4_usb_midi_cin_t;

typedef struct {
    uint8_t cable;
    uint8_t cin;
    uint8_t len;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} flx4_midi_message_t;

typedef void (*flx4_midi_message_cb_t)(const flx4_midi_message_t *msg, void *user_ctx);

bool flx4_midi_parse_usb_packet(const uint8_t packet[4], flx4_midi_message_t *out);

bool flx4_midi_find_streaming_in_endpoint(const uint8_t *config_desc,
                                          size_t config_len,
                                          uint8_t *interface_num,
                                          uint8_t *alternate_setting,
                                          uint8_t *in_ep_addr,
                                          uint16_t *in_ep_mps);

void flx4_midi_host_set_message_callback(flx4_midi_message_cb_t cb, void *user_ctx);

esp_err_t flx4_midi_host_init(void);
