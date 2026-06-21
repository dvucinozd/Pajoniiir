#include "flx4_midi_host.h"

#include <inttypes.h>
#include <string.h>

static flx4_midi_message_cb_t s_message_cb;
static void *s_message_cb_ctx;
static bool s_connection_state_valid;
static bool s_connection_state_connected;

void flx4_midi_host_set_message_callback(flx4_midi_message_cb_t cb, void *user_ctx)
{
    s_message_cb = cb;
    s_message_cb_ctx = user_ctx;
}

static bool should_publish_connection_state(bool connected)
{
    if (!s_connection_state_valid) {
        s_connection_state_valid = true;
        s_connection_state_connected = connected;
        return connected;
    }
    if (s_connection_state_connected == connected) {
        return false;
    }
    s_connection_state_connected = connected;
    return true;
}

static uint8_t cin_payload_len(uint8_t cin)
{
    switch (cin) {
    case FLX4_USB_MIDI_CIN_2BYTE_SYSTEM:
    case FLX4_USB_MIDI_CIN_SYSEX_END_2:
    case FLX4_USB_MIDI_CIN_PROGRAM_CHANGE:
    case FLX4_USB_MIDI_CIN_CHANNEL_PRESSURE:
        return 2;
    case FLX4_USB_MIDI_CIN_3BYTE_SYSTEM:
    case FLX4_USB_MIDI_CIN_SYSEX_START:
    case FLX4_USB_MIDI_CIN_SYSEX_END_3:
    case FLX4_USB_MIDI_CIN_NOTE_OFF:
    case FLX4_USB_MIDI_CIN_NOTE_ON:
    case FLX4_USB_MIDI_CIN_POLY_PRESSURE:
    case FLX4_USB_MIDI_CIN_CONTROL_CHANGE:
    case FLX4_USB_MIDI_CIN_PITCH_BEND:
        return 3;
    case FLX4_USB_MIDI_CIN_SYSEX_END_1:
    case FLX4_USB_MIDI_CIN_SINGLE_BYTE:
        return 1;
    default:
        return 0;
    }
}

bool flx4_midi_parse_usb_packet(const uint8_t packet[4], flx4_midi_message_t *out)
{
    if (!packet || !out) {
        return false;
    }

    const uint8_t cin = packet[0] & 0x0F;
    const uint8_t len = cin_payload_len(cin);
    if (len == 0) {
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

#define FLX4_USB_DESC_TYPE_CONFIG     0x02
#define FLX4_USB_DESC_TYPE_INTERFACE  0x04
#define FLX4_USB_DESC_TYPE_ENDPOINT   0x05
#define FLX4_USB_CLASS_AUDIO          0x01
#define FLX4_MIDI_STREAM_SUBCLASS     0x03
#define FLX4_USB_EP_DIR_IN_MASK       0x80
#define FLX4_USB_EP_XFER_TYPE_MASK    0x03
#define FLX4_USB_EP_XFER_BULK         0x02
#define FLX4_USB_EP_XFER_INTR         0x03

bool flx4_midi_find_streaming_in_endpoint(const uint8_t *config_desc,
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
    bool in_midi_streaming_interface = false;

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
            in_midi_streaming_interface =
                config_desc[offset + 5] == FLX4_USB_CLASS_AUDIO &&
                config_desc[offset + 6] == FLX4_MIDI_STREAM_SUBCLASS;
            if (in_midi_streaming_interface) {
                *interface_num = config_desc[offset + 2];
                *alternate_setting = config_desc[offset + 3];
            }
        } else if (in_midi_streaming_interface && type == FLX4_USB_DESC_TYPE_ENDPOINT) {
            if (len < 7) {
                return false;
            }
            uint8_t ep_addr = config_desc[offset + 2];
            uint8_t xfer_type = config_desc[offset + 3] & FLX4_USB_EP_XFER_TYPE_MASK;
            bool is_stream_endpoint = xfer_type == FLX4_USB_EP_XFER_BULK ||
                                      xfer_type == FLX4_USB_EP_XFER_INTR;
            if ((ep_addr & FLX4_USB_EP_DIR_IN_MASK) && is_stream_endpoint) {
                *in_ep_addr = ep_addr;
                *in_ep_mps = (uint16_t)config_desc[offset + 4] |
                             ((uint16_t)config_desc[offset + 5] << 8);
                return true;
            }
        }

        offset += len;
    }

    return false;
}

bool flx4_midi_find_streaming_endpoints(const uint8_t *config_desc,
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
    bool in_midi_streaming_interface = false;
    bool found_in = false;
    bool found_out = false;

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
            in_midi_streaming_interface =
                config_desc[offset + 5] == FLX4_USB_CLASS_AUDIO &&
                config_desc[offset + 6] == FLX4_MIDI_STREAM_SUBCLASS;
            if (in_midi_streaming_interface) {
                *interface_num = config_desc[offset + 2];
                *alternate_setting = config_desc[offset + 3];
            }
        } else if (in_midi_streaming_interface && type == FLX4_USB_DESC_TYPE_ENDPOINT) {
            if (len < 7) {
                return false;
            }
            uint8_t ep_addr = config_desc[offset + 2];
            uint8_t xfer_type = config_desc[offset + 3] & FLX4_USB_EP_XFER_TYPE_MASK;
            bool is_stream_endpoint = xfer_type == FLX4_USB_EP_XFER_BULK ||
                                      xfer_type == FLX4_USB_EP_XFER_INTR;
            if (is_stream_endpoint) {
                if (ep_addr & FLX4_USB_EP_DIR_IN_MASK) {
                    *in_ep_addr = ep_addr;
                    *in_ep_mps = (uint16_t)config_desc[offset + 4] |
                                 ((uint16_t)config_desc[offset + 5] << 8);
                    found_in = true;
                } else {
                    *out_ep_addr = ep_addr;
                    *out_ep_mps = (uint16_t)config_desc[offset + 4] |
                                  ((uint16_t)config_desc[offset + 5] << 8);
                    found_out = true;
                }
            }
        }

        offset += len;
    }

    return found_in && found_out;
}

bool flx4_midi_host_is_vu_meter_packet(const uint8_t packet[4])
{
    if (!packet) {
        return false;
    }

    const uint8_t cin = packet[0] & 0x0F;
    const uint8_t status = packet[1];
    const uint8_t controller = packet[2];

    return cin == FLX4_USB_MIDI_CIN_CONTROL_CHANGE &&
           (status == 0xB0 || status == 0xB1) &&
           controller == 0x02;
}

bool flx4_midi_host_should_drop_out_packet(const uint8_t packet[4],
                                           uint32_t queue_spaces,
                                           uint32_t queue_capacity)
{
    if (!packet || queue_capacity == 0) {
        return false;
    }

    if (!flx4_midi_host_is_vu_meter_packet(packet)) {
        return false;
    }

    return queue_spaces < queue_capacity;
}

#if defined(FLX4_MIDI_HOST_PC_TEST)
void flx4_midi_host_test_reset_connection_state(void)
{
    s_connection_state_valid = false;
    s_connection_state_connected = false;
}

bool flx4_midi_host_test_publish_connection_state(
    bool connected,
    flx4_midi_host_test_connection_event_t *out)
{
    if (!out || !should_publish_connection_state(connected)) {
        return false;
    }
    out->type = 0x82;
    out->id = 0x70;
    out->value = connected ? 1 : 0;
    return true;
}

esp_err_t flx4_midi_host_init(void)
{
    return ESP_OK;
}
esp_err_t flx4_midi_host_send_packet(const uint8_t packet[4])
{
    (void)packet;
    return ESP_OK;
}
#else

#include "esp_check.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "control_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

static const char *TAG = "flx4_host";

#define USB_LIB_TASK_STACK      4096
#define USB_LIB_TASK_PRIO       4
#define MIDI_CLIENT_TASK_STACK  (6 * 1024)
#define MIDI_CLIENT_TASK_PRIO   5
#define CLIENT_NUM_EVENT_MSG    5
#define MIDI_TRANSFER_BYTES     64
#define MIDI_OUT_QUEUE_DEPTH    32

static void publish_connection_state(bool connected)
{
    if (!should_publish_connection_state(connected)) {
        return;
    }

    const int16_t value = connected ? CTRL_FLX4_CONNECTED : CTRL_FLX4_DISCONNECTED;
    esp_err_t rc = control_link_send_semantic(CTRL_TYPE_STATE, CTRL_ID_FLX4_CONNECTION, value);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "publish FLX4 connection state failed: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGI(TAG, "published FLX4 connection state: %s",
                 connected ? "connected" : "disconnected");
    }
}

typedef struct {
    usb_host_client_handle_t client_hdl;
    usb_device_handle_t dev_hdl;
    usb_transfer_t *in_xfer;
    usb_transfer_t *out_xfer;
    uint8_t pending_dev_addr;
    uint8_t interface_num;
    uint8_t alternate_setting;
    uint8_t in_ep_addr;
    uint16_t in_ep_mps;
    uint8_t out_ep_addr;
    uint16_t out_ep_mps;
    bool has_pending_dev;
    bool opened;
    bool claimed;
    bool transfer_active;
    bool out_transfer_active;
    bool closing;
} flx4_host_state_t;

static flx4_host_state_t s_host;
static QueueHandle_t s_midi_out_queue = NULL;
static SemaphoreHandle_t s_midi_out_mutex = NULL;

static void log_midi_packet(const uint8_t packet[4])
{
    flx4_midi_message_t msg;
    if (!flx4_midi_parse_usb_packet(packet, &msg)) {
        ESP_LOGW(TAG, "USB-MIDI raw %02X %02X %02X %02X (unsupported CIN)",
                 packet[0], packet[1], packet[2], packet[3]);
        return;
    }

    ESP_LOGD(TAG,
             "USB-MIDI cable=%u cin=0x%X len=%u status=0x%02X data1=0x%02X data2=0x%02X",
             msg.cable, msg.cin, msg.len, msg.status, msg.data1, msg.data2);
    if (s_message_cb) {
        s_message_cb(&msg, s_message_cb_ctx);
    }
}

static void midi_in_transfer_cb(usb_transfer_t *transfer)
{
    flx4_host_state_t *host = (flx4_host_state_t *)transfer->context;
    host->transfer_active = false;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        for (int offset = 0; offset + 3 < transfer->actual_num_bytes; offset += 4) {
            log_midi_packet(&transfer->data_buffer[offset]);
        }
    } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "MIDI IN transfer status=%d bytes=%d", transfer->status, transfer->actual_num_bytes);
    }

    if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE || host->closing) {
        if (host->in_xfer == transfer) {
            esp_err_t rc = usb_host_transfer_free(host->in_xfer);
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "free MIDI IN transfer after disconnect: %s", esp_err_to_name(rc));
            }
            host->in_xfer = NULL;
        }
        return;
    }

    if (host->opened && host->claimed && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        transfer->num_bytes = usb_round_up_to_mps(MIDI_TRANSFER_BYTES, host->in_ep_mps);
        esp_err_t rc = usb_host_transfer_submit(transfer);
        if (rc == ESP_OK) {
            host->transfer_active = true;
        } else {
            ESP_LOGE(TAG, "resubmit MIDI IN transfer failed: %s", esp_err_to_name(rc));
        }
    }
}

static void midi_out_transfer_cb(usb_transfer_t *transfer)
{
    flx4_host_state_t *host = (flx4_host_state_t *)transfer->context;

    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED &&
        transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "MIDI OUT transfer status=%d", transfer->status);
    }

    if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE || host->closing) {
        if (host->out_xfer == transfer) {
            esp_err_t rc = usb_host_transfer_free(host->out_xfer);
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "free MIDI OUT transfer after disconnect: %s", esp_err_to_name(rc));
            }
            host->out_xfer = NULL;
        }
        return;
    }

    if (s_midi_out_mutex && xSemaphoreTake(s_midi_out_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        host->out_transfer_active = false;

        uint8_t next_packet[4];
        if (s_midi_out_queue && xQueueReceive(s_midi_out_queue, next_packet, 0) == pdTRUE) {
            memcpy(transfer->data_buffer, next_packet, 4);
            transfer->num_bytes = 4;
            esp_err_t rc = usb_host_transfer_submit(transfer);
            if (rc == ESP_OK) {
                host->out_transfer_active = true;
            } else {
                ESP_LOGE(TAG, "resubmit MIDI OUT transfer failed: %s", esp_err_to_name(rc));
            }
        }
        xSemaphoreGive(s_midi_out_mutex);
    } else {
        host->out_transfer_active = false;
    }
}


static bool find_midi_streaming_endpoints(const usb_config_desc_t *cfg,
                                         uint8_t *interface_num,
                                         uint8_t *alternate_setting,
                                         uint8_t *in_ep_addr,
                                         uint16_t *in_ep_mps,
                                         uint8_t *out_ep_addr,
                                         uint16_t *out_ep_mps)
{
    const uint8_t *base = (const uint8_t *)cfg;
    int offset = cfg->bLength;
    bool in_midi_streaming_interface = false;
    bool found_in = false;
    bool found_out = false;

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
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)std;
            in_midi_streaming_interface =
                intf->bInterfaceClass == USB_CLASS_AUDIO &&
                intf->bInterfaceSubClass == FLX4_MIDI_STREAM_SUBCLASS;
            ESP_LOGI(TAG,
                     "interface=%u alt=%u class=0x%02X subclass=0x%02X endpoints=%u%s",
                     intf->bInterfaceNumber,
                     intf->bAlternateSetting,
                     intf->bInterfaceClass,
                     intf->bInterfaceSubClass,
                     intf->bNumEndpoints,
                     in_midi_streaming_interface ? " MIDIStreaming" : "");
            if (in_midi_streaming_interface) {
                *interface_num = intf->bInterfaceNumber;
                *alternate_setting = intf->bAlternateSetting;
            }
        } else if (in_midi_streaming_interface &&
                   std->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
                   std->bLength >= USB_EP_DESC_SIZE) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)std;
            const bool is_in = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
            const uint8_t xfer_type = ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
            const bool is_stream_endpoint =
                xfer_type == USB_BM_ATTRIBUTES_XFER_BULK ||
                xfer_type == USB_BM_ATTRIBUTES_XFER_INT;
            ESP_LOGI(TAG,
                     "  endpoint=0x%02X attr=0x%02X mps=%u interval=%u",
                     ep->bEndpointAddress,
                     ep->bmAttributes,
                     USB_EP_DESC_GET_MPS(ep),
                     ep->bInterval);
            if (is_stream_endpoint) {
                if (is_in) {
                    *in_ep_addr = ep->bEndpointAddress;
                    *in_ep_mps = USB_EP_DESC_GET_MPS(ep);
                    found_in = true;
                } else {
                    *out_ep_addr = ep->bEndpointAddress;
                    *out_ep_mps = USB_EP_DESC_GET_MPS(ep);
                    found_out = true;
                }
            }
        }

        offset += std->bLength;
    }

    return found_in && found_out;
}

static esp_err_t start_midi_in_transfer(flx4_host_state_t *host)
{
    const int transfer_bytes = usb_round_up_to_mps(MIDI_TRANSFER_BYTES, host->in_ep_mps);
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(transfer_bytes, 0, &host->in_xfer),
                        TAG, "alloc transfer");

    host->in_xfer->device_handle = host->dev_hdl;
    host->in_xfer->bEndpointAddress = host->in_ep_addr;
    host->in_xfer->callback = midi_in_transfer_cb;
    host->in_xfer->context = host;
    host->in_xfer->num_bytes = transfer_bytes;

    ESP_RETURN_ON_ERROR(usb_host_transfer_submit(host->in_xfer), TAG, "submit MIDI IN");
    host->transfer_active = true;
    ESP_LOGI(TAG, "listening for raw USB-MIDI packets on endpoint 0x%02X", host->in_ep_addr);
    return ESP_OK;
}

static void close_device(flx4_host_state_t *host)
{
    host->closing = true;
    if (host->in_xfer && !host->transfer_active) {
        esp_err_t rc = usb_host_transfer_free(host->in_xfer);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "free MIDI IN transfer: %s", esp_err_to_name(rc));
        }
        host->in_xfer = NULL;
    }
    if (host->out_xfer && !host->out_transfer_active) {
        esp_err_t rc = usb_host_transfer_free(host->out_xfer);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "free MIDI OUT transfer: %s", esp_err_to_name(rc));
        }
        host->out_xfer = NULL;
    }
    if (host->claimed) {
        esp_err_t rc = usb_host_interface_release(host->client_hdl, host->dev_hdl, host->interface_num);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "release MIDI interface: %s", esp_err_to_name(rc));
        }
        host->claimed = false;
    }
    if (host->opened) {
        esp_err_t rc = usb_host_device_close(host->client_hdl, host->dev_hdl);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "close USB device: %s", esp_err_to_name(rc));
        }
        host->dev_hdl = NULL;
        host->opened = false;
    }
    host->in_ep_addr = 0;
    host->in_ep_mps = 0;
    host->out_ep_addr = 0;
    host->out_ep_mps = 0;
    host->transfer_active = false;
    host->out_transfer_active = false;
    publish_connection_state(false);
    ESP_LOGW(TAG, "DDJ-FLX4 device closed/disconnected");
}

static esp_err_t open_device(flx4_host_state_t *host, uint8_t dev_addr)
{
    ESP_LOGI(TAG, "opening USB device addr=%u", dev_addr);
    host->closing = false;
    ESP_RETURN_ON_ERROR(usb_host_device_open(host->client_hdl, dev_addr, &host->dev_hdl),
                        TAG, "open device");
    host->opened = true;

    usb_device_info_t info;
    if (usb_host_device_info(host->dev_hdl, &info) == ESP_OK) {
        ESP_LOGI(TAG, "device speed=%d config=%u address=%u",
                 info.speed, info.bConfigurationValue, info.dev_addr);
    }

    const usb_device_desc_t *dev_desc = NULL;
    ESP_RETURN_ON_ERROR(usb_host_get_device_descriptor(host->dev_hdl, &dev_desc),
                        TAG, "device descriptor");
    ESP_LOGI(TAG,
             "descriptor VID=0x%04X PID=0x%04X class=0x%02X subclass=0x%02X configs=%u",
             dev_desc->idVendor,
             dev_desc->idProduct,
             dev_desc->bDeviceClass,
             dev_desc->bDeviceSubClass,
             dev_desc->bNumConfigurations);

    const usb_config_desc_t *cfg = NULL;
    ESP_RETURN_ON_ERROR(usb_host_get_active_config_descriptor(host->dev_hdl, &cfg),
                        TAG, "config descriptor");
    ESP_LOGI(TAG, "active config value=%u interfaces=%u total_len=%u",
             cfg->bConfigurationValue, cfg->bNumInterfaces, cfg->wTotalLength);

    if (!find_midi_streaming_endpoints(cfg,
                                       &host->interface_num,
                                       &host->alternate_setting,
                                       &host->in_ep_addr,
                                       &host->in_ep_mps,
                                       &host->out_ep_addr,
                                       &host->out_ep_mps)) {
        ESP_LOGE(TAG, "no MIDIStreaming IN/OUT endpoints found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(usb_host_interface_claim(host->client_hdl,
                                                host->dev_hdl,
                                                host->interface_num,
                                                host->alternate_setting),
                        TAG, "claim MIDI interface");
    host->claimed = true;
    ESP_LOGI(TAG, "claimed MIDIStreaming interface=%u alt=%u",
             host->interface_num, host->alternate_setting);

    // Alokacija out_xfer
    const int out_transfer_bytes = usb_round_up_to_mps(MIDI_TRANSFER_BYTES, host->out_ep_mps);
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(out_transfer_bytes, 0, &host->out_xfer),
                        TAG, "alloc out transfer");
    host->out_xfer->device_handle = host->dev_hdl;
    host->out_xfer->bEndpointAddress = host->out_ep_addr;
    host->out_xfer->callback = midi_out_transfer_cb;
    host->out_xfer->context = host;
    host->out_transfer_active = false;

    ESP_LOGI(TAG, "MIDI OUT endpoint 0x%02X registered", host->out_ep_addr);

    esp_err_t rc = start_midi_in_transfer(host);
    if (rc == ESP_OK) {
        publish_connection_state(true);
    }
    return rc;
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    flx4_host_state_t *host = (flx4_host_state_t *)arg;

    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        host->pending_dev_addr = event_msg->new_dev.address;
        host->has_pending_dev = true;
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        close_device(host);
        break;
    default:
        break;
    }
}

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t rc = usb_host_install(&host_cfg);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(rc));
        vTaskDelete(NULL);
        return;
    }

    xTaskNotifyGive((TaskHandle_t)arg);
    ESP_LOGI(TAG, "USB host library installed for DDJ-FLX4 MIDI capture");

    while (1) {
        uint32_t flags = 0;
        rc = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(rc));
            continue;
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static void midi_client_task(void *arg)
{
    (void)arg;

    memset(&s_host, 0, sizeof(s_host));
    const usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = &s_host,
        },
    };

    ESP_ERROR_CHECK(usb_host_client_register(&client_cfg, &s_host.client_hdl));
    ESP_LOGI(TAG, "USB host client registered; connect the DDJ-FLX4");

    while (1) {
        usb_host_client_handle_events(s_host.client_hdl, pdMS_TO_TICKS(100));
        if (s_host.has_pending_dev) {
            const uint8_t addr = s_host.pending_dev_addr;
            s_host.has_pending_dev = false;
            if (s_host.opened) {
                ESP_LOGW(TAG, "already handling a USB MIDI device; ignoring addr=%u", addr);
                continue;
            }
            esp_err_t rc = open_device(&s_host, addr);
            if (rc != ESP_OK) {
                ESP_LOGE(TAG, "device open/probe failed: %s", esp_err_to_name(rc));
                close_device(&s_host);
            }
        }
    }
}

esp_err_t flx4_midi_host_init(void)
{
    s_midi_out_queue = xQueueCreate(32, 4);
    if (!s_midi_out_queue) {
        return ESP_ERR_NO_MEM;
    }
    s_midi_out_mutex = xSemaphoreCreateMutex();
    if (!s_midi_out_mutex) {
        vQueueDelete(s_midi_out_queue);
        s_midi_out_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t usb_task_hdl = NULL;
    if (xTaskCreate(usb_lib_task, "usb_host", USB_LIB_TASK_STACK,
                    xTaskGetCurrentTaskHandle(), USB_LIB_TASK_PRIO, &usb_task_hdl) != pdPASS) {
        vSemaphoreDelete(s_midi_out_mutex);
        s_midi_out_mutex = NULL;
        vQueueDelete(s_midi_out_queue);
        s_midi_out_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0) {
        ESP_LOGE(TAG, "USB host library did not start");
        return ESP_ERR_TIMEOUT;
    }

    if (xTaskCreate(midi_client_task, "flx4_midi", MIDI_CLIENT_TASK_STACK,
                    NULL, MIDI_CLIENT_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "DDJ-FLX4 USB MIDI host started");
    return ESP_OK;
}

esp_err_t flx4_midi_host_send_packet(const uint8_t packet[4])
{
    if (!s_midi_out_queue || !s_midi_out_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_midi_out_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    if (!s_host.opened || !s_host.claimed || s_host.closing) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    const uint32_t queue_spaces = (uint32_t)uxQueueSpacesAvailable(s_midi_out_queue);
    const bool drop_silently =
        flx4_midi_host_should_drop_out_packet(packet, queue_spaces, MIDI_OUT_QUEUE_DEPTH);
    if (drop_silently) {
        goto exit;
    }

    if (xQueueSend(s_midi_out_queue, packet, 0) != pdTRUE) {
        if (flx4_midi_host_is_vu_meter_packet(packet)) {
            goto exit;
        }
        ESP_LOGW(TAG, "MIDI OUT queue full, dropping packet");
        ret = ESP_ERR_TIMEOUT;
        goto exit;
    }

    if (!s_host.out_transfer_active && s_host.out_xfer) {
        uint8_t next_packet[4];
        if (xQueueReceive(s_midi_out_queue, next_packet, 0) == pdTRUE) {
            memcpy(s_host.out_xfer->data_buffer, next_packet, 4);
            s_host.out_xfer->num_bytes = 4;
            s_host.out_xfer->device_handle = s_host.dev_hdl;
            s_host.out_xfer->bEndpointAddress = s_host.out_ep_addr;
            esp_err_t rc = usb_host_transfer_submit(s_host.out_xfer);
            if (rc == ESP_OK) {
                s_host.out_transfer_active = true;
            } else {
                ESP_LOGE(TAG, "submit MIDI OUT transfer failed: %s", esp_err_to_name(rc));
                ret = rc;
            }
        }
    }

exit:
    xSemaphoreGive(s_midi_out_mutex);
    return ret;
}

#endif
