#include "flx4_usb_audio.h"

#include <string.h>

#if !defined(FLX4_USB_AUDIO_PC_TEST)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "flx4_uac_packetizer.h"

#ifndef CONFIG_DDJ_FLX4_USB_AUDIO_TONE_ON_CHANNELS_1_2
#define CONFIG_DDJ_FLX4_USB_AUDIO_TONE_ON_CHANNELS_1_2 0
#endif

typedef enum {
    FLX4_USB_AUDIO_MODE_STOPPED = 0,
    FLX4_USB_AUDIO_MODE_TONE,
    FLX4_USB_AUDIO_MODE_RING,
} flx4_usb_audio_mode_t;

static const int16_t s_sine_48_minus18dbfs[48] = {
    0, 539, 1069, 1581, 2066, 2515, 2921, 3276,
    3575, 3813, 3985, 4090, 4125, 4090, 3985, 3813,
    3575, 3276, 2921, 2515, 2066, 1581, 1069, 539,
    0, -539, -1069, -1581, -2066, -2515, -2921, -3276,
    -3575, -3813, -3985, -4090, -4125, -4090, -3985, -3813,
    -3575, -3276, -2921, -2515, -2066, -1581, -1069, -539,
};

static flx4_usb_audio_stats_t s_stats;
static flx4_uac_packetizer_t s_packetizer;
static flx4_usb_audio_mode_t s_mode;
static uint32_t s_stream_sample_rate;
static uint32_t s_tone_phase_q16;
static uint32_t s_tone_step_q16;

#if !defined(FLX4_USB_AUDIO_PC_TEST)
static const char *TAG = "flx4_usb_audio";
#define FLX4_USB_AUDIO_TRANSFER_COUNT 4u
#define FLX4_USB_AUDIO_PACKETS_PER_TRANSFER 8u
#define FLX4_USB_AUDIO_CTRL_TIMEOUT_MS 500u
#define FLX4_USB_AUDIO_MAX_CONSECUTIVE_ERRORS 100u
#define FLX4_USB_AUDIO_STATS_LOG_INTERVAL_MS 5000u

/* UAC 1.0 class request: SET_CUR of SAMPLING_FREQ_CONTROL on the iso endpoint. */
#define UAC_REQ_SET_CUR 0x01u
#define UAC_SAMPLING_FREQ_CONTROL 0x01u

static usb_transfer_t *s_transfers[FLX4_USB_AUDIO_TRANSFER_COUNT];
static bool s_streaming;
static usb_host_client_handle_t s_client;
static usb_device_handle_t s_device;
static bool s_interface_claimed;
static uint8_t s_claimed_interface_num;
static volatile bool s_ctrl_done;
static volatile usb_transfer_status_t s_ctrl_status;
static uint32_t s_consecutive_errors;
static TickType_t s_last_stats_log_tick;
#endif

static bool format_has_rate(const flx4_uac_playback_format_t *fmt, uint32_t rate)
{
    if (!fmt) {
        return false;
    }
    for (uint8_t i = 0; i < fmt->sample_rate_count; ++i) {
        if (fmt->sample_rates[i] == rate) {
            return true;
        }
    }
    return false;
}

static uint32_t selected_stream_rate(const flx4_uac_playback_format_t *fmt)
{
    if (format_has_rate(fmt, 48000u)) {
        return 48000u;
    }
    if (format_has_rate(fmt, 44100u)) {
        return 44100u;
    }
    return (fmt && fmt->sample_rate_count > 0u) ? fmt->sample_rates[0] : 0u;
}

static int16_t next_tone_sample(void)
{
    const uint32_t index = (s_tone_phase_q16 >> 16) % 48u;
    s_tone_phase_q16 += s_tone_step_q16;
    return s_sine_48_minus18dbfs[index];
}

#if !defined(FLX4_USB_AUDIO_PC_TEST)
static void submit_tone_transfer(usb_transfer_t *transfer);

static void ctrl_transfer_cb(usb_transfer_t *transfer)
{
    s_ctrl_status = transfer->status;
    s_ctrl_done = true;
}

/*
 * Must only run in the FLX4 client task while usb_host_client_handle_events()
 * is NOT on the call stack: completion callbacks are dispatched only when this
 * function pumps client events itself. Safe from flx4_usb_audio_configure()
 * (called from open_device() in the client task loop); NOT safe from
 * client_event_cb() context.
 */
static esp_err_t send_control_request(uint8_t bmRequestType,
                                      uint8_t bRequest,
                                      uint16_t wValue,
                                      uint16_t wIndex,
                                      const uint8_t *data,
                                      uint16_t wLength)
{
    usb_host_client_handle_t client = s_client;
    usb_device_handle_t device = s_device;
    if (!client || !device) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_transfer_t *ctrl = NULL;
    esp_err_t rc = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &ctrl);
    if (rc != ESP_OK) {
        return rc;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)ctrl->data_buffer;
    setup->bmRequestType = bmRequestType;
    setup->bRequest = bRequest;
    setup->wValue = wValue;
    setup->wIndex = wIndex;
    setup->wLength = wLength;
    if (wLength > 0u && data) {
        memcpy(&ctrl->data_buffer[sizeof(usb_setup_packet_t)], data, wLength);
    }
    ctrl->num_bytes = (int)(sizeof(usb_setup_packet_t) + wLength);
    ctrl->device_handle = device;
    ctrl->bEndpointAddress = 0;
    ctrl->callback = ctrl_transfer_cb;
    ctrl->context = NULL;

    s_ctrl_done = false;
    s_ctrl_status = USB_TRANSFER_STATUS_ERROR;
    rc = usb_host_transfer_submit_control(client, ctrl);
    if (rc != ESP_OK) {
        (void)usb_host_transfer_free(ctrl);
        return rc;
    }

    TickType_t waited = 0;
    const TickType_t step = pdMS_TO_TICKS(10);
    while (!s_ctrl_done && waited < pdMS_TO_TICKS(FLX4_USB_AUDIO_CTRL_TIMEOUT_MS)) {
        (void)usb_host_client_handle_events(client, step);
        waited += step;
    }

    if (!s_ctrl_done) {
        /* The host stack may still own the transfer; freeing now would hand
           the controller a dangling buffer, so leak it instead. */
        ESP_LOGE(TAG, "control request 0x%02X/0x%02X timed out",
                 (unsigned)bmRequestType, (unsigned)bRequest);
        return ESP_ERR_TIMEOUT;
    }

    const bool completed = s_ctrl_status == USB_TRANSFER_STATUS_COMPLETED;
    (void)usb_host_transfer_free(ctrl);
    return completed ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_device_interface_alt(uint8_t interface_num, uint8_t alt_setting)
{
    return send_control_request(USB_BM_REQUEST_TYPE_DIR_OUT |
                                    USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                                    USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                                USB_B_REQUEST_SET_INTERFACE,
                                alt_setting,
                                interface_num,
                                NULL,
                                0u);
}

static esp_err_t set_endpoint_sample_rate(uint8_t endpoint_addr, uint32_t sample_rate)
{
    const uint8_t rate_bytes[3] = {
        (uint8_t)(sample_rate & 0xFFu),
        (uint8_t)((sample_rate >> 8) & 0xFFu),
        (uint8_t)((sample_rate >> 16) & 0xFFu),
    };
    return send_control_request(USB_BM_REQUEST_TYPE_DIR_OUT |
                                    USB_BM_REQUEST_TYPE_TYPE_CLASS |
                                    USB_BM_REQUEST_TYPE_RECIP_ENDPOINT,
                                UAC_REQ_SET_CUR,
                                (uint16_t)(UAC_SAMPLING_FREQ_CONTROL << 8),
                                endpoint_addr,
                                rate_bytes,
                                (uint16_t)sizeof(rate_bytes));
}

static void log_stream_stats_if_due(void)
{
    const TickType_t now = xTaskGetTickCount();
    if ((now - s_last_stats_log_tick) < pdMS_TO_TICKS(FLX4_USB_AUDIO_STATS_LOG_INTERVAL_MS)) {
        return;
    }
    s_last_stats_log_tick = now;
    ESP_LOGI(TAG, "FLX4_USB_AUDIO tx submitted=%u completed=%u skipped=%u underrun=%u bytes=%u",
             (unsigned)s_stats.submitted_packets,
             (unsigned)s_stats.completed_packets,
             (unsigned)s_stats.skipped_packets,
             (unsigned)s_stats.underrun_packets,
             (unsigned)s_stats.actual_bytes);
}

static void audio_transfer_cb(usb_transfer_t *transfer)
{
    if (!transfer) {
        return;
    }

    if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE || !s_streaming) {
        return;
    }

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        s_consecutive_errors = 0u;
        for (int i = 0; i < transfer->num_isoc_packets; ++i) {
            if (transfer->isoc_packet_desc[i].status == USB_TRANSFER_STATUS_COMPLETED) {
                s_stats.completed_packets++;
            } else if (transfer->isoc_packet_desc[i].status == USB_TRANSFER_STATUS_SKIPPED) {
                s_stats.skipped_packets++;
            } else {
                s_stats.underrun_packets++;
            }
        }
    } else {
        s_stats.underrun_packets++;
        s_consecutive_errors++;
        if (s_consecutive_errors == 1u ||
            (s_consecutive_errors % FLX4_USB_AUDIO_MAX_CONSECUTIVE_ERRORS) == 0u) {
            ESP_LOGW(TAG, "audio transfer error status=%d consecutive=%u",
                     (int)transfer->status, (unsigned)s_consecutive_errors);
        }
        if (s_consecutive_errors >= FLX4_USB_AUDIO_MAX_CONSECUTIVE_ERRORS) {
            ESP_LOGE(TAG, "stopping FLX4 USB audio stream after %u consecutive transfer errors",
                     (unsigned)s_consecutive_errors);
            s_streaming = false;
            return;
        }
    }

    log_stream_stats_if_due();
    submit_tone_transfer(transfer);
}

static void free_transfers(void)
{
    s_streaming = false;
    for (uint8_t i = 0; i < FLX4_USB_AUDIO_TRANSFER_COUNT; ++i) {
        if (s_transfers[i]) {
            esp_err_t rc = usb_host_transfer_free(s_transfers[i]);
            if (rc == ESP_OK) {
                s_transfers[i] = NULL;
            } else {
                /* Still owned by the host stack (in flight); keep the pointer
                   so the next configure pass can retry the free. */
                ESP_LOGW(TAG, "audio transfer %u still in flight; deferring free", (unsigned)i);
            }
        }
    }
}

static esp_err_t allocate_transfers(const flx4_uac_playback_format_t *fmt)
{
    if (!fmt || fmt->max_packet_size == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < FLX4_USB_AUDIO_TRANSFER_COUNT; ++i) {
        if (s_transfers[i]) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    const size_t bytes_per_transfer = (size_t)fmt->max_packet_size * FLX4_USB_AUDIO_PACKETS_PER_TRANSFER;
    for (uint8_t i = 0; i < FLX4_USB_AUDIO_TRANSFER_COUNT; ++i) {
        esp_err_t rc = usb_host_transfer_alloc(bytes_per_transfer,
                                               FLX4_USB_AUDIO_PACKETS_PER_TRANSFER,
                                               &s_transfers[i]);
        if (rc != ESP_OK) {
            free_transfers();
            return rc;
        }
        s_transfers[i]->bEndpointAddress = fmt->endpoint_addr;
        s_transfers[i]->callback = audio_transfer_cb;
    }
    return ESP_OK;
}

static void submit_tone_transfer(usb_transfer_t *transfer)
{
    if (!transfer || s_mode != FLX4_USB_AUDIO_MODE_TONE) {
        return;
    }

    size_t offset = 0u;
    for (int i = 0; i < transfer->num_isoc_packets; ++i) {
        size_t bytes = flx4_usb_audio_fill_next_tone_packet(&transfer->data_buffer[offset],
                                                            transfer->data_buffer_size - offset);
        transfer->isoc_packet_desc[i].num_bytes = (int)bytes;
        transfer->isoc_packet_desc[i].actual_num_bytes = 0;
        offset += bytes;
    }

    if (offset == 0u) {
        s_stats.skipped_packets++;
        return;
    }

    transfer->num_bytes = (int)offset;
    esp_err_t rc = usb_host_transfer_submit(transfer);
    if (rc != ESP_OK) {
        s_stats.skipped_packets++;
        ESP_LOGW(TAG, "submit tone transfer failed: %s", esp_err_to_name(rc));
    }
}

static esp_err_t start_tone_transfers(void)
{
    s_streaming = true;
    s_consecutive_errors = 0u;
    s_last_stats_log_tick = xTaskGetTickCount();
    for (uint8_t i = 0; i < FLX4_USB_AUDIO_TRANSFER_COUNT; ++i) {
        if (!s_transfers[i]) {
            s_streaming = false;
            return ESP_ERR_INVALID_STATE;
        }
        submit_tone_transfer(s_transfers[i]);
    }
    return ESP_OK;
}
#endif

esp_err_t flx4_usb_audio_configure(usb_host_client_handle_t client,
                                   usb_device_handle_t device,
                                   const uint8_t *config_desc,
                                   size_t config_len)
{
    flx4_uac_descriptor_result_t parsed = { 0 };
    flx4_uac_playback_format_t selected = { 0 };
    if (!flx4_uac_parse_playback_formats(config_desc, config_len, &parsed) ||
        !flx4_uac_select_preferred_format(&parsed, &selected)) {
        flx4_usb_audio_stop();
        return ESP_ERR_NOT_FOUND;
    }

#if !defined(FLX4_USB_AUDIO_PC_TEST)
    free_transfers();
    s_client = client;
    s_device = device;
    esp_err_t claim_rc = usb_host_interface_claim(client,
                                                  device,
                                                  selected.interface_num,
                                                  selected.alternate_setting);
    if (claim_rc != ESP_OK) {
        flx4_usb_audio_stop();
        return claim_rc;
    }
    s_interface_claimed = true;
    s_claimed_interface_num = selected.interface_num;

    /* usb_host_interface_claim() only allocates host-side pipes; the device
       stays on the zero-bandwidth alt 0 until SET_INTERFACE reaches the bus. */
    esp_err_t alt_rc = set_device_interface_alt(selected.interface_num,
                                                selected.alternate_setting);
    if (alt_rc != ESP_OK) {
        ESP_LOGE(TAG, "SET_INTERFACE %u alt %u failed: %s",
                 (unsigned)selected.interface_num,
                 (unsigned)selected.alternate_setting,
                 esp_err_to_name(alt_rc));
        flx4_usb_audio_stop();
        return alt_rc;
    }

    const uint32_t stream_rate = selected_stream_rate(&selected);
    esp_err_t rate_rc = set_endpoint_sample_rate(selected.endpoint_addr, stream_rate);
    if (rate_rc != ESP_OK) {
        ESP_LOGE(TAG, "SET_CUR sampling freq %u on ep 0x%02X failed: %s",
                 (unsigned)stream_rate,
                 (unsigned)selected.endpoint_addr,
                 esp_err_to_name(rate_rc));
        flx4_usb_audio_stop();
        return rate_rc;
    }
    ESP_LOGI(TAG, "FLX4 interface %u alt %u active, ep 0x%02X, %u Hz, %u ch, %u bit",
             (unsigned)selected.interface_num,
             (unsigned)selected.alternate_setting,
             (unsigned)selected.endpoint_addr,
             (unsigned)stream_rate,
             (unsigned)selected.channels,
             (unsigned)selected.bits_per_sample);

    esp_err_t alloc_rc = allocate_transfers(&selected);
    if (alloc_rc != ESP_OK) {
        flx4_usb_audio_stop();
        return alloc_rc;
    }
    for (uint8_t i = 0; i < FLX4_USB_AUDIO_TRANSFER_COUNT; ++i) {
        s_transfers[i]->device_handle = device;
    }
#else
    (void)client;
    (void)device;
#endif

    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.configured = true;
    s_stats.format = selected;
    s_stream_sample_rate = selected_stream_rate(&selected);
    flx4_uac_packetizer_init(&s_packetizer,
                             s_stream_sample_rate,
                             selected.channels,
                             selected.bytes_per_sample);
    s_mode = FLX4_USB_AUDIO_MODE_STOPPED;
    s_tone_phase_q16 = 0u;
    s_tone_step_q16 = 0u;
    return ESP_OK;
}

esp_err_t flx4_usb_audio_start_tone(uint16_t hz)
{
    if (!s_stats.configured || s_stream_sample_rate == 0u || hz == 0u) {
        return ESP_ERR_INVALID_STATE;
    }

    s_tone_phase_q16 = 0u;
    s_tone_step_q16 = ((uint32_t)hz * 48u * 65536u) / s_stream_sample_rate;
    s_mode = FLX4_USB_AUDIO_MODE_TONE;
#if !defined(FLX4_USB_AUDIO_PC_TEST)
    return start_tone_transfers();
#else
    return ESP_OK;
#endif
}

esp_err_t flx4_usb_audio_start_ring(void)
{
    if (!s_stats.configured) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mode = FLX4_USB_AUDIO_MODE_RING;
    return ESP_OK;
}

void flx4_usb_audio_stop(void)
{
#if !defined(FLX4_USB_AUDIO_PC_TEST)
    s_streaming = false;
    free_transfers();
    /* Host-side release only: this runs from client_event_cb (DEV_GONE)
       context where pumping client events for a SET_INTERFACE(alt 0) control
       transfer would re-enter usb_host_client_handle_events(). Without the
       release, usb_host_device_close() fails and the device object leaks. */
    if (s_interface_claimed && s_client && s_device) {
        esp_err_t rc = usb_host_interface_release(s_client, s_device, s_claimed_interface_num);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "release audio interface %u: %s",
                     (unsigned)s_claimed_interface_num, esp_err_to_name(rc));
        }
    }
    s_interface_claimed = false;
    s_claimed_interface_num = 0u;
    s_client = NULL;
    s_device = NULL;
    s_consecutive_errors = 0u;
#endif
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_packetizer, 0, sizeof(s_packetizer));
    s_mode = FLX4_USB_AUDIO_MODE_STOPPED;
    s_stream_sample_rate = 0u;
    s_tone_phase_q16 = 0u;
    s_tone_step_q16 = 0u;
}

void flx4_usb_audio_get_stats(flx4_usb_audio_stats_t *out)
{
    if (out) {
        *out = s_stats;
    }
}

size_t flx4_usb_audio_fill_next_tone_packet(uint8_t *dst, size_t dst_capacity)
{
    if (!dst || s_mode != FLX4_USB_AUDIO_MODE_TONE ||
        s_stats.format.channels == 0u || s_stats.format.bytes_per_sample != 2u) {
        return 0u;
    }

    const uint16_t frames = flx4_uac_packetizer_next_frames(&s_packetizer);
    const size_t packet_bytes = (size_t)frames *
                                (size_t)s_stats.format.channels *
                                (size_t)s_stats.format.bytes_per_sample;
    if (frames == 0u || dst_capacity < packet_bytes) {
        s_stats.skipped_packets++;
        return 0u;
    }

    memset(dst, 0, packet_bytes);
    for (uint16_t frame = 0; frame < frames; ++frame) {
        const int16_t sample = next_tone_sample();
        uint8_t first_tone_channel = 0u;
        if (s_stats.format.channels >= 4u && !CONFIG_DDJ_FLX4_USB_AUDIO_TONE_ON_CHANNELS_1_2) {
            first_tone_channel = 2u;
        }

        for (uint8_t channel = 0u; channel < 2u && first_tone_channel + channel < s_stats.format.channels; ++channel) {
            const size_t offset = (((size_t)frame * s_stats.format.channels) +
                                   (size_t)first_tone_channel + channel) * sizeof(int16_t);
            memcpy(&dst[offset], &sample, sizeof(sample));
        }
    }

    s_stats.submitted_packets++;
    s_stats.actual_bytes += (uint32_t)packet_bytes;
    return packet_bytes;
}
