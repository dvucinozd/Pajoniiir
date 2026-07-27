#!/usr/bin/env python3
"""Apply S3 MIDI ownership and descriptor parsing fixes from the release audit."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_function(text: str, signature: str, replacement: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise RuntimeError(f"missing function: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"missing function body: {signature}")
    depth = 0
    end = None
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                end = pos + 1
                break
    if end is None:
        raise RuntimeError(f"unterminated function: {signature}")
    return text[:start] + replacement.rstrip() + text[end:]


def patch_source() -> None:
    path = ROOT / "firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c"
    text = path.read_text(encoding="utf-8")

    text = replace_once(
        text,
        """static bool s_connection_state_valid;\nstatic bool s_connection_state_connected;\n""",
        """static bool s_connection_state_valid;\nstatic bool s_connection_state_connected;\nstatic bool s_connection_refresh_requested;\n""",
        "connection refresh request state",
    )

    text = replace_function(
        text,
        "static bool should_publish_connection_state(bool connected)",
        r'''static bool should_publish_connection_state(bool connected)
{
    const bool valid = __atomic_load_n(&s_connection_state_valid, __ATOMIC_ACQUIRE);
    const bool current = __atomic_load_n(&s_connection_state_connected, __ATOMIC_ACQUIRE);
    if (!valid) {
        __atomic_store_n(&s_connection_state_connected, connected, __ATOMIC_RELEASE);
        __atomic_store_n(&s_connection_state_valid, true, __ATOMIC_RELEASE);
        return connected;
    }
    if (current == connected) {
        return false;
    }
    __atomic_store_n(&s_connection_state_connected, connected, __ATOMIC_RELEASE);
    return true;
}''',
    )
    text = replace_function(
        text,
        "static bool should_refresh_connection_state(void)",
        r'''static bool should_refresh_connection_state(void)
{
    return __atomic_load_n(&s_connection_state_valid, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&s_connection_state_connected, __ATOMIC_ACQUIRE);
}''',
    )

    text = replace_function(
        text,
        "bool flx4_midi_find_streaming_in_endpoint(const uint8_t *config_desc,",
        r'''bool flx4_midi_find_streaming_in_endpoint(const uint8_t *config_desc,
                                          size_t config_len,
                                          uint8_t *interface_num,
                                          uint8_t *alternate_setting,
                                          uint8_t *in_ep_addr,
                                          uint16_t *in_ep_mps)
{
    if (!config_desc || config_len < 9 ||
        !interface_num || !alternate_setting || !in_ep_addr || !in_ep_mps) {
        return false;
    }
    if (config_desc[1] != FLX4_USB_DESC_TYPE_CONFIG) {
        return false;
    }

    size_t total_len = (size_t)config_desc[2] | ((size_t)config_desc[3] << 8);
    if (total_len == 0 || total_len > config_len) {
        total_len = config_len;
    }

    size_t offset = config_desc[0];
    bool candidate_active = false;
    uint8_t candidate_interface = 0;
    uint8_t candidate_alt = 0;

    while (offset + 2 <= total_len) {
        uint8_t len = config_desc[offset];
        uint8_t type = config_desc[offset + 1];
        if (len < 2 || offset + len > total_len) {
            return false;
        }

        if (type == FLX4_USB_DESC_TYPE_INTERFACE) {
            if (len < 9) {
                return false;
            }
            candidate_active =
                config_desc[offset + 5] == FLX4_USB_CLASS_AUDIO &&
                config_desc[offset + 6] == FLX4_MIDI_STREAM_SUBCLASS;
            candidate_interface = config_desc[offset + 2];
            candidate_alt = config_desc[offset + 3];
        } else if (candidate_active && type == FLX4_USB_DESC_TYPE_ENDPOINT) {
            if (len < 7) {
                return false;
            }
            const uint8_t ep_addr = config_desc[offset + 2];
            const uint8_t xfer_type = config_desc[offset + 3] & FLX4_USB_EP_XFER_TYPE_MASK;
            const uint16_t mps = (uint16_t)config_desc[offset + 4] |
                                 ((uint16_t)config_desc[offset + 5] << 8);
            const bool is_stream_endpoint = xfer_type == FLX4_USB_EP_XFER_BULK ||
                                            xfer_type == FLX4_USB_EP_XFER_INTR;
            if ((ep_addr & FLX4_USB_EP_DIR_IN_MASK) && is_stream_endpoint && mps != 0u) {
                *interface_num = candidate_interface;
                *alternate_setting = candidate_alt;
                *in_ep_addr = ep_addr;
                *in_ep_mps = mps;
                return true;
            }
        }

        offset += len;
    }

    return false;
}''',
    )

    text = replace_function(
        text,
        "bool flx4_midi_find_streaming_endpoints(const uint8_t *config_desc,",
        r'''bool flx4_midi_find_streaming_endpoints(const uint8_t *config_desc,
                                        size_t config_len,
                                        uint8_t *interface_num,
                                        uint8_t *alternate_setting,
                                        uint8_t *in_ep_addr,
                                        uint16_t *in_ep_mps,
                                        uint8_t *out_ep_addr,
                                        uint16_t *out_ep_mps)
{
    if (!config_desc || config_len < 9 ||
        !interface_num || !alternate_setting ||
        !in_ep_addr || !in_ep_mps ||
        !out_ep_addr || !out_ep_mps) {
        return false;
    }
    if (config_desc[1] != FLX4_USB_DESC_TYPE_CONFIG) {
        return false;
    }

    size_t total_len = (size_t)config_desc[2] | ((size_t)config_desc[3] << 8);
    if (total_len == 0 || total_len > config_len) {
        total_len = config_len;
    }

    size_t offset = config_desc[0];
    bool candidate_active = false;
    bool found_in = false;
    bool found_out = false;
    uint8_t candidate_interface = 0;
    uint8_t candidate_alt = 0;
    uint8_t candidate_in = 0;
    uint8_t candidate_out = 0;
    uint16_t candidate_in_mps = 0;
    uint16_t candidate_out_mps = 0;

    while (offset + 2 <= total_len) {
        uint8_t len = config_desc[offset];
        uint8_t type = config_desc[offset + 1];
        if (len < 2 || offset + len > total_len) {
            return false;
        }

        if (type == FLX4_USB_DESC_TYPE_INTERFACE) {
            if (len < 9) {
                return false;
            }
            if (candidate_active && found_in && found_out) {
                *interface_num = candidate_interface;
                *alternate_setting = candidate_alt;
                *in_ep_addr = candidate_in;
                *in_ep_mps = candidate_in_mps;
                *out_ep_addr = candidate_out;
                *out_ep_mps = candidate_out_mps;
                return true;
            }

            candidate_active =
                config_desc[offset + 5] == FLX4_USB_CLASS_AUDIO &&
                config_desc[offset + 6] == FLX4_MIDI_STREAM_SUBCLASS;
            candidate_interface = config_desc[offset + 2];
            candidate_alt = config_desc[offset + 3];
            found_in = false;
            found_out = false;
            candidate_in = 0;
            candidate_out = 0;
            candidate_in_mps = 0;
            candidate_out_mps = 0;
        } else if (candidate_active && type == FLX4_USB_DESC_TYPE_ENDPOINT) {
            if (len < 7) {
                return false;
            }
            const uint8_t ep_addr = config_desc[offset + 2];
            const uint8_t xfer_type = config_desc[offset + 3] & FLX4_USB_EP_XFER_TYPE_MASK;
            const uint16_t mps = (uint16_t)config_desc[offset + 4] |
                                 ((uint16_t)config_desc[offset + 5] << 8);
            const bool is_stream_endpoint = xfer_type == FLX4_USB_EP_XFER_BULK ||
                                            xfer_type == FLX4_USB_EP_XFER_INTR;
            if (is_stream_endpoint && mps != 0u) {
                if (ep_addr & FLX4_USB_EP_DIR_IN_MASK) {
                    candidate_in = ep_addr;
                    candidate_in_mps = mps;
                    found_in = true;
                } else {
                    candidate_out = ep_addr;
                    candidate_out_mps = mps;
                    found_out = true;
                }
            }
        }

        offset += len;
    }

    if (!(candidate_active && found_in && found_out)) {
        return false;
    }
    *interface_num = candidate_interface;
    *alternate_setting = candidate_alt;
    *in_ep_addr = candidate_in;
    *in_ep_mps = candidate_in_mps;
    *out_ep_addr = candidate_out;
    *out_ep_mps = candidate_out_mps;
    return true;
}''',
    )

    text = replace_once(
        text,
        """    s_connection_state_valid = false;\n    s_connection_state_connected = false;\n""",
        """    __atomic_store_n(&s_connection_state_valid, false, __ATOMIC_RELEASE);\n    __atomic_store_n(&s_connection_state_connected, false, __ATOMIC_RELEASE);\n    __atomic_store_n(&s_connection_refresh_requested, false, __ATOMIC_RELEASE);\n""",
        "PC connection reset",
    )

    old_runtime_refresh = r'''bool flx4_midi_host_refresh_connection_state(void)
{
    if (!should_refresh_connection_state()) {
        return false;
    }

    esp_err_t rc = control_link_send_semantic(CTRL_TYPE_STATE,
                                              CTRL_ID_FLX4_CONNECTION,
                                              CTRL_FLX4_CONNECTED);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "refresh FLX4 connection state failed: %s", esp_err_to_name(rc));
        return false;
    }
    desc_report_send_if_valid();
    return true;
}'''
    new_runtime_refresh = r'''static void publish_connection_refresh_from_usb_owner(void)
{
    if (!should_refresh_connection_state() || !s_host.opened || !s_host.claimed || s_host.closing) {
        return;
    }
    esp_err_t rc = control_link_send_semantic(CTRL_TYPE_STATE,
                                              CTRL_ID_FLX4_CONNECTION,
                                              CTRL_FLX4_CONNECTED);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "refresh FLX4 connection state failed: %s", esp_err_to_name(rc));
        return;
    }
    desc_report_send_if_valid();
}

bool flx4_midi_host_refresh_connection_state(void)
{
    if (!should_refresh_connection_state()) {
        return false;
    }
    __atomic_store_n(&s_connection_refresh_requested, true, __ATOMIC_RELEASE);
    usb_host_client_handle_t client =
        __atomic_load_n(&s_midi_client_handle, __ATOMIC_ACQUIRE);
    if (client) {
        (void)usb_host_client_unblock(client);
    }
    return true;
}'''
    text = replace_once(text, old_runtime_refresh, new_runtime_refresh, "USB owner refresh request")

    text = replace_function(
        text,
        "static bool find_midi_streaming_endpoints(const usb_config_desc_t *cfg,",
        r'''static bool find_midi_streaming_endpoints(const usb_config_desc_t *cfg,
                                          uint8_t *interface_num,
                                          uint8_t *alternate_setting,
                                          uint8_t *in_ep_addr,
                                          uint16_t *in_ep_mps,
                                          uint8_t *out_ep_addr,
                                          uint16_t *out_ep_mps)
{
    const uint8_t *base = (const uint8_t *)cfg;
    int offset = cfg->bLength;
    bool candidate_active = false;
    bool found_in = false;
    bool found_out = false;
    uint8_t candidate_interface = 0;
    uint8_t candidate_alt = 0;
    uint8_t candidate_in = 0;
    uint8_t candidate_out = 0;
    uint16_t candidate_in_mps = 0;
    uint16_t candidate_out_mps = 0;

    while (offset + USB_STANDARD_DESC_SIZE <= cfg->wTotalLength) {
        const usb_standard_desc_t *std = (const usb_standard_desc_t *)(base + offset);
        if (std->bLength < USB_STANDARD_DESC_SIZE) {
            break;
        }
        if (offset + std->bLength > cfg->wTotalLength) {
            ESP_LOGW(TAG, "truncated USB descriptor at offset=%d len=%u total=%u",
                     offset, std->bLength, cfg->wTotalLength);
            break;
        }

        if (std->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE &&
            std->bLength >= USB_INTF_DESC_SIZE) {
            if (candidate_active && found_in && found_out) {
                *interface_num = candidate_interface;
                *alternate_setting = candidate_alt;
                *in_ep_addr = candidate_in;
                *in_ep_mps = candidate_in_mps;
                *out_ep_addr = candidate_out;
                *out_ep_mps = candidate_out_mps;
                return true;
            }

            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)std;
            candidate_active =
                intf->bInterfaceClass == USB_CLASS_AUDIO &&
                intf->bInterfaceSubClass == FLX4_MIDI_STREAM_SUBCLASS;
            candidate_interface = intf->bInterfaceNumber;
            candidate_alt = intf->bAlternateSetting;
            found_in = false;
            found_out = false;
            candidate_in = 0;
            candidate_out = 0;
            candidate_in_mps = 0;
            candidate_out_mps = 0;

            ESP_LOGI(TAG,
                     "interface=%u alt=%u class=0x%02X subclass=0x%02X endpoints=%u%s",
                     intf->bInterfaceNumber,
                     intf->bAlternateSetting,
                     intf->bInterfaceClass,
                     intf->bInterfaceSubClass,
                     intf->bNumEndpoints,
                     candidate_active ? " MIDIStreaming" : "");
        } else if (candidate_active &&
                   std->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
                   std->bLength >= USB_EP_DESC_SIZE) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)std;
            const bool is_in = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
            const uint8_t xfer_type = ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
            const uint16_t mps = USB_EP_DESC_GET_MPS(ep);
            const bool is_stream_endpoint =
                xfer_type == USB_BM_ATTRIBUTES_XFER_BULK ||
                xfer_type == USB_BM_ATTRIBUTES_XFER_INT;
            ESP_LOGI(TAG,
                     "  endpoint=0x%02X attr=0x%02X mps=%u interval=%u",
                     ep->bEndpointAddress, ep->bmAttributes, mps, ep->bInterval);
            if (is_stream_endpoint && mps != 0u) {
                if (is_in) {
                    candidate_in = ep->bEndpointAddress;
                    candidate_in_mps = mps;
                    found_in = true;
                } else {
                    candidate_out = ep->bEndpointAddress;
                    candidate_out_mps = mps;
                    found_out = true;
                }
            }
        }

        offset += std->bLength;
    }

    if (!(candidate_active && found_in && found_out)) {
        return false;
    }
    *interface_num = candidate_interface;
    *alternate_setting = candidate_alt;
    *in_ep_addr = candidate_in;
    *in_ep_mps = candidate_in_mps;
    *out_ep_addr = candidate_out;
    *out_ep_mps = candidate_out_mps;
    return true;
}''',
    )

    loop_old = """        usb_host_client_handle_events(s_host.client_hdl, pdMS_TO_TICKS(100));\n        if (s_host.closing) {\n            (void)close_device_step(&s_host);\n            continue;\n        }\n"""
    loop_new = """        usb_host_client_handle_events(s_host.client_hdl, pdMS_TO_TICKS(100));\n        if (s_host.closing) {\n            __atomic_store_n(&s_connection_refresh_requested, false, __ATOMIC_RELEASE);\n            (void)close_device_step(&s_host);\n            continue;\n        }\n        if (__atomic_exchange_n(&s_connection_refresh_requested, false, __ATOMIC_ACQ_REL)) {\n            publish_connection_refresh_from_usb_owner();\n        }\n"""
    text = replace_once(text, loop_old, loop_new, "USB owner consumes refresh")

    path.write_text(text, encoding="utf-8")


def patch_tests() -> None:
    path = ROOT / "tests/flx4_midi_host/test_flx4_midi_host.c"
    text = path.read_text(encoding="utf-8")

    anchor = r'''static void test_rejects_truncated_descriptor(void)
{'''
    tests = r'''static void test_midi_in_and_out_must_share_interface_and_alt(void)
{
    const uint8_t split_cfg[] = {
        9, 2, 55, 0, 3, 1, 0, 0x80, 50,
        9, 4, 1, 0, 1, 0x01, 0x03, 0x00, 0,
        7, 5, 0x81, 0x02, 64, 0, 0,
        9, 4, 2, 0, 1, 0x01, 0x03, 0x00, 0,
        7, 5, 0x02, 0x02, 64, 0, 0,
        9, 4, 3, 0, 0, 0xFF, 0x00, 0x00, 0,
    };
    uint8_t interface_num = 0;
    uint8_t alternate_setting = 0;
    uint8_t in_ep_addr = 0;
    uint16_t in_ep_mps = 0;
    uint8_t out_ep_addr = 0;
    uint16_t out_ep_mps = 0;

    assert(!flx4_midi_find_streaming_endpoints(split_cfg, sizeof(split_cfg),
                                                &interface_num, &alternate_setting,
                                                &in_ep_addr, &in_ep_mps,
                                                &out_ep_addr, &out_ep_mps));
}

static void test_midi_endpoint_parser_rejects_zero_mps(void)
{
    const uint8_t cfg[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 1, 0, 2, 0x01, 0x03, 0x00, 0,
        7, 5, 0x81, 0x02, 0, 0, 0,
        7, 5, 0x02, 0x02, 64, 0, 0,
    };
    uint8_t interface_num = 0;
    uint8_t alternate_setting = 0;
    uint8_t in_ep_addr = 0;
    uint16_t in_ep_mps = 0;
    uint8_t out_ep_addr = 0;
    uint16_t out_ep_mps = 0;

    assert(!flx4_midi_find_streaming_in_endpoint(cfg, sizeof(cfg),
                                                 &interface_num, &alternate_setting,
                                                 &in_ep_addr, &in_ep_mps));
    assert(!flx4_midi_find_streaming_endpoints(cfg, sizeof(cfg),
                                               &interface_num, &alternate_setting,
                                               &in_ep_addr, &in_ep_mps,
                                               &out_ep_addr, &out_ep_mps));
}

static void test_finds_midi_in_and_out_on_same_alt(void)
{
    const uint8_t cfg[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 4, 2, 2, 0x01, 0x03, 0x00, 0,
        7, 5, 0x84, 0x02, 64, 0, 0,
        7, 5, 0x04, 0x02, 64, 0, 0,
    };
    uint8_t interface_num = 0;
    uint8_t alternate_setting = 0;
    uint8_t in_ep_addr = 0;
    uint16_t in_ep_mps = 0;
    uint8_t out_ep_addr = 0;
    uint16_t out_ep_mps = 0;

    assert(flx4_midi_find_streaming_endpoints(cfg, sizeof(cfg),
                                              &interface_num, &alternate_setting,
                                              &in_ep_addr, &in_ep_mps,
                                              &out_ep_addr, &out_ep_mps));
    assert(interface_num == 4);
    assert(alternate_setting == 2);
    assert(in_ep_addr == 0x84 && in_ep_mps == 64);
    assert(out_ep_addr == 0x04 && out_ep_mps == 64);
}

static void test_rejects_truncated_descriptor(void)
{'''
    text = replace_once(text, anchor, tests, "descriptor ownership tests")

    main_old = """    test_finds_midi_streaming_in_endpoint();\n    test_rejects_truncated_descriptor();\n"""
    main_new = """    test_finds_midi_streaming_in_endpoint();\n    test_midi_in_and_out_must_share_interface_and_alt();\n    test_midi_endpoint_parser_rejects_zero_mps();\n    test_finds_midi_in_and_out_on_same_alt();\n    test_rejects_truncated_descriptor();\n"""
    text = replace_once(text, main_old, main_new, "register descriptor tests")

    path.write_text(text, encoding="utf-8")


def main() -> None:
    patch_source()
    patch_tests()
    print("Applied S3 MIDI ownership and parser fixes.")


if __name__ == "__main__":
    main()
