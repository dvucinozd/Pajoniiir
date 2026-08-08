/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t interface_num;
    uint8_t alternate_setting;
    uint8_t in_ep_addr;
    uint16_t in_ep_mps;
    uint8_t out_ep_addr;
    uint16_t out_ep_mps;
} usb_midi_endpoints_t;

typedef struct {
    uint8_t cable;
    uint8_t cin;
    uint8_t len;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} usb_midi_message_t;

bool usb_midi_find_streaming_endpoints(const uint8_t *config,
                                       size_t config_len,
                                       usb_midi_endpoints_t *out);
bool usb_midi_config_has_interface_class(const uint8_t *config,
                                         size_t config_len,
                                         uint8_t interface_class,
                                         uint8_t interface_subclass);
bool usb_midi_parse_event_packet(const uint8_t packet[4],
                                 usb_midi_message_t *out);

#ifdef __cplusplus
}
#endif
