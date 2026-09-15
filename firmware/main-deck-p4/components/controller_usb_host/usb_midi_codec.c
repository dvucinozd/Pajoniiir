/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_midi_codec.h"

#include <string.h>

#define USB_DESC_CONFIGURATION 0x02u
#define USB_DESC_INTERFACE     0x04u
#define USB_DESC_ENDPOINT      0x05u
#define USB_CLASS_AUDIO        0x01u
#define USB_SUBCLASS_MIDI      0x03u
#define USB_ENDPOINT_IN        0x80u
#define USB_TRANSFER_MASK      0x03u
#define USB_TRANSFER_BULK      0x02u
#define USB_TRANSFER_INTERRUPT 0x03u
#define USB_SUBCLASS_ANY       0xFFu

static bool descriptor_total_length(const uint8_t *config,
                                    size_t config_len,
                                    size_t *total_out)
{
    if (!config || !total_out || config_len < 9u || config[0] < 9u ||
        config[1] != USB_DESC_CONFIGURATION) {
        return false;
    }
    const size_t total = (size_t)config[2] | ((size_t)config[3] << 8u);
    if (total < config[0] || total > config_len) {
        return false;
    }
    *total_out = total;
    return true;
}

bool usb_midi_config_has_interface_class(const uint8_t *config,
                                         size_t config_len,
                                         uint8_t interface_class,
                                         uint8_t interface_subclass)
{
    size_t total = 0u;
    if (!descriptor_total_length(config, config_len, &total)) {
        return false;
    }
    for (size_t offset = config[0]; offset + 2u <= total;) {
        const uint8_t length = config[offset];
        const uint8_t type = config[offset + 1u];
        if (length < 2u || offset + length > total) {
            return false;
        }
        if (type == USB_DESC_INTERFACE && length >= 9u &&
            config[offset + 5u] == interface_class &&
            (interface_subclass == USB_SUBCLASS_ANY ||
             config[offset + 6u] == interface_subclass)) {
            return true;
        }
        offset += length;
    }
    return false;
}

bool usb_midi_find_streaming_endpoints(const uint8_t *config,
                                       size_t config_len,
                                       usb_midi_endpoints_t *out)
{
    size_t total = 0u;
    if (!out || !descriptor_total_length(config, config_len, &total)) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    bool candidate = false;
    bool have_in = false;
    bool have_out = false;
    usb_midi_endpoints_t current = {0};

    for (size_t offset = config[0]; offset + 2u <= total;) {
        const uint8_t length = config[offset];
        const uint8_t type = config[offset + 1u];
        if (length < 2u || offset + length > total) {
            return false;
        }
        if (type == USB_DESC_INTERFACE && length >= 9u) {
            if (candidate && have_in && have_out) {
                *out = current;
                return true;
            }
            candidate = config[offset + 5u] == USB_CLASS_AUDIO &&
                        config[offset + 6u] == USB_SUBCLASS_MIDI;
            memset(&current, 0, sizeof(current));
            current.interface_num = config[offset + 2u];
            current.alternate_setting = config[offset + 3u];
            have_in = false;
            have_out = false;
        } else if (candidate && type == USB_DESC_ENDPOINT && length >= 7u) {
            const uint8_t address = config[offset + 2u];
            const uint8_t transfer_type = config[offset + 3u] & USB_TRANSFER_MASK;
            const uint16_t mps = (uint16_t)config[offset + 4u] |
                                 ((uint16_t)config[offset + 5u] << 8u);
            const bool stream_endpoint = transfer_type == USB_TRANSFER_BULK ||
                                         transfer_type == USB_TRANSFER_INTERRUPT;
            if (stream_endpoint && mps != 0u) {
                if ((address & USB_ENDPOINT_IN) != 0u) {
                    current.in_ep_addr = address;
                    current.in_ep_mps = mps;
                    have_in = true;
                } else {
                    current.out_ep_addr = address;
                    current.out_ep_mps = mps;
                    have_out = true;
                }
            }
        }
        offset += length;
    }
    if (!(candidate && have_in && have_out)) {
        return false;
    }
    *out = current;
    return true;
}

bool usb_midi_parse_event_packet(const uint8_t packet[4],
                                 usb_midi_message_t *out)
{
    static const uint8_t lengths[16] = {
        0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
    };
    if (!packet || !out) {
        return false;
    }
    const uint8_t cin = packet[0] & 0x0Fu;
    const uint8_t len = lengths[cin];
    if (len == 0u) {
        return false;
    }
    *out = (usb_midi_message_t) {
        .cable = (uint8_t)(packet[0] >> 4u),
        .cin = cin,
        .len = len,
        .status = packet[1],
        .data1 = packet[2],
        .data2 = packet[3],
    };
    return true;
}
