#include "usb_midi_probe.h"

#include <string.h>

#define USB_DESC_TYPE_CONFIG       0x02u
#define USB_DESC_TYPE_INTERFACE    0x04u
#define USB_DESC_TYPE_ENDPOINT     0x05u
#define USB_CLASS_AUDIO            0x01u
#define USB_MIDI_STREAM_SUBCLASS   0x03u
#define USB_EP_DIR_IN              0x80u
#define USB_EP_XFER_MASK           0x03u
#define USB_EP_XFER_BULK           0x02u
#define USB_EP_XFER_INTERRUPT      0x03u
#define USB_EP_MPS_MASK            0x07FFu

static bool config_bounds(const uint8_t *config_desc,
                          size_t config_len,
                          size_t *start,
                          size_t *total)
{
    if (!config_desc || !start || !total || config_len < 9u) {
        return false;
    }
    const uint8_t header_len = config_desc[0];
    if (header_len < 9u || header_len > config_len ||
        config_desc[1] != USB_DESC_TYPE_CONFIG) {
        return false;
    }
    const size_t declared = (size_t)config_desc[2] |
                            ((size_t)config_desc[3] << 8);
    if (declared < header_len || declared > config_len) {
        return false;
    }
    *start = header_len;
    *total = declared;
    return true;
}

bool usb_midi_find_streaming_endpoints(const uint8_t *config_desc,
                                       size_t config_len,
                                       usb_midi_endpoints_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    size_t offset = 0u;
    size_t total = 0u;
    if (!config_bounds(config_desc, config_len, &offset, &total)) {
        return false;
    }

    bool midi_interface = false;
    bool found_in = false;
    bool found_out = false;
    usb_midi_endpoints_t candidate = {0};

    while (offset + 2u <= total) {
        const uint8_t len = config_desc[offset];
        const uint8_t type = config_desc[offset + 1u];
        if (len < 2u || offset + len > total) {
            return false;
        }

        if (type == USB_DESC_TYPE_INTERFACE) {
            if (len < 9u) {
                return false;
            }
            if (midi_interface && found_in && found_out) {
                *out = candidate;
                return true;
            }

            midi_interface =
                config_desc[offset + 5u] == USB_CLASS_AUDIO &&
                config_desc[offset + 6u] == USB_MIDI_STREAM_SUBCLASS;
            found_in = false;
            found_out = false;
            memset(&candidate, 0, sizeof(candidate));
            candidate.interface_num = config_desc[offset + 2u];
            candidate.alternate_setting = config_desc[offset + 3u];
        } else if (midi_interface && type == USB_DESC_TYPE_ENDPOINT) {
            if (len < 7u) {
                return false;
            }
            const uint8_t ep_addr = config_desc[offset + 2u];
            const uint8_t transfer_type =
                config_desc[offset + 3u] & USB_EP_XFER_MASK;
            const uint16_t mps =
                ((uint16_t)config_desc[offset + 4u] |
                 ((uint16_t)config_desc[offset + 5u] << 8)) & USB_EP_MPS_MASK;
            const bool supported = transfer_type == USB_EP_XFER_BULK ||
                                   transfer_type == USB_EP_XFER_INTERRUPT;
            if (supported && mps != 0u) {
                if ((ep_addr & USB_EP_DIR_IN) != 0u) {
                    candidate.in_ep_addr = ep_addr;
                    candidate.in_ep_mps = mps;
                    found_in = true;
                } else {
                    candidate.out_ep_addr = ep_addr;
                    candidate.out_ep_mps = mps;
                    found_out = true;
                }
            }
        }

        offset += len;
    }

    if (!(midi_interface && found_in && found_out)) {
        return false;
    }
    *out = candidate;
    return true;
}

bool usb_midi_config_has_interface_class(const uint8_t *config_desc,
                                         size_t config_len,
                                         uint8_t interface_class,
                                         uint8_t interface_subclass)
{
    size_t offset = 0u;
    size_t total = 0u;
    if (!config_bounds(config_desc, config_len, &offset, &total)) {
        return false;
    }

    while (offset + 2u <= total) {
        const uint8_t len = config_desc[offset];
        const uint8_t type = config_desc[offset + 1u];
        if (len < 2u || offset + len > total) {
            return false;
        }
        if (type == USB_DESC_TYPE_INTERFACE) {
            if (len < 9u) {
                return false;
            }
            const bool class_match = config_desc[offset + 5u] == interface_class;
            const bool subclass_match = interface_subclass == 0xFFu ||
                                        config_desc[offset + 6u] == interface_subclass;
            if (class_match && subclass_match) {
                return true;
            }
        }
        offset += len;
    }
    return false;
}

static uint8_t cin_payload_len(uint8_t cin)
{
    switch (cin) {
    case 0x02u: /* two-byte system common */
    case 0x06u: /* SysEx end with two bytes */
    case 0x0Cu: /* program change */
    case 0x0Du: /* channel pressure */
        return 2u;
    case 0x03u: /* three-byte system common */
    case 0x04u: /* SysEx start/continue */
    case 0x07u: /* SysEx end with three bytes */
    case 0x08u: /* note off */
    case 0x09u: /* note on */
    case 0x0Au: /* poly pressure */
    case 0x0Bu: /* control change */
    case 0x0Eu: /* pitch bend */
        return 3u;
    case 0x05u: /* SysEx end with one byte */
    case 0x0Fu: /* single byte */
        return 1u;
    default:
        return 0u;
    }
}

bool usb_midi_parse_event_packet(const uint8_t packet[4],
                                 usb_midi_message_t *out)
{
    if (!packet || !out) {
        return false;
    }
    const uint8_t cin = packet[0] & 0x0Fu;
    const uint8_t len = cin_payload_len(cin);
    if (len == 0u) {
        return false;
    }

    out->cable = packet[0] >> 4;
    out->cin = cin;
    out->len = len;
    out->status = packet[1];
    out->data1 = packet[2];
    out->data2 = packet[3];
    return true;
}
