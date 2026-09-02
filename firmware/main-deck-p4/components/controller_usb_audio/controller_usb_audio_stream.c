/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_usb_audio_stream.h"

#include <string.h>

#include "controller_audio_resampler.h"
#include "controller_audio_ring.h"
#include "esp_log.h"
#include "flx4_uac_descriptors.h"
#include "flx4_uac_packetizer.h"

#define STREAM_RATE_HZ 44100u
#define STREAM_CHANNELS 4u
#define STREAM_BYTES_PER_SAMPLE 2u
#define STREAM_RING_FRAMES 2048u
#define STREAM_TRANSFER_COUNT 3u
#define STREAM_PACKETS_PER_TRANSFER 4
#define RESAMPLE_INPUT_FRAMES 128u
#define RESAMPLE_OUTPUT_FRAMES 129u

static const char *TAG = "controller_uac";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static controller_audio_ring_t s_ring;
static int16_t s_ring_storage[STREAM_RING_FRAMES * STREAM_CHANNELS];
static controller_audio_resampler_t s_resampler;
static flx4_uac_packetizer_t s_packetizer;
static flx4_uac_playback_format_t s_format;
static usb_host_client_handle_t s_client;
static usb_device_handle_t s_device;
static usb_transfer_t *s_control;
static usb_transfer_t *s_isoc[STREAM_TRANSFER_COUNT];
static bool s_isoc_active[STREAM_TRANSFER_COUNT];
static TaskHandle_t s_owner_task;
static UBaseType_t s_active_priority;
static UBaseType_t s_transition_priority;
static uint8_t s_control_step;
static bool s_control_active;
static bool s_claimed;
static bool s_configuring;
static bool s_streaming;
static bool s_stopping;
static bool s_device_gone;
static bool s_flush_attempted;
static bool s_faulted;
static bool s_accepting;
static uint64_t s_submitted_blocks;
static uint64_t s_dropped_blocks;
static uint64_t s_submitted_frames;
static uint32_t s_config_failures;
static uint32_t s_transfer_failures;

static void lower_to_transition_priority(void)
{
    if (s_owner_task && s_transition_priority > 0u) {
        vTaskPrioritySet(s_owner_task, s_transition_priority);
    }
}

static bool format_supports_rate(const flx4_uac_playback_format_t *format,
                                 uint32_t rate)
{
    if (!format) {
        return false;
    }
    for (uint8_t i = 0u; i < format->sample_rate_count; ++i) {
        if (format->sample_rates[i] == rate) {
            return true;
        }
    }
    return false;
}

static bool select_stream_format(const uint8_t *descriptor,
                                 size_t descriptor_length,
                                 flx4_uac_playback_format_t *out)
{
    flx4_uac_descriptor_result_t parsed;
    if (!flx4_uac_parse_playback_formats(descriptor, descriptor_length,
                                         &parsed)) {
        return false;
    }
    for (uint8_t i = 0u; i < parsed.format_count; ++i) {
        const flx4_uac_playback_format_t *candidate = &parsed.formats[i];
        const uint32_t packet_bytes =
            45u * STREAM_CHANNELS * STREAM_BYTES_PER_SAMPLE;
        if (candidate->channels == STREAM_CHANNELS &&
            candidate->bits_per_sample == 16u &&
            candidate->bytes_per_sample == STREAM_BYTES_PER_SAMPLE &&
            candidate->max_packet_size >= packet_bytes &&
            format_supports_rate(candidate, STREAM_RATE_HZ)) {
            *out = *candidate;
            return true;
        }
    }
    return false;
}

static int transfer_index(const usb_transfer_t *transfer)
{
    for (unsigned i = 0u; i < STREAM_TRANSFER_COUNT; ++i) {
        if (s_isoc[i] == transfer) {
            return (int)i;
        }
    }
    return -1;
}

static bool has_active_isoc(void)
{
    for (unsigned i = 0u; i < STREAM_TRANSFER_COUNT; ++i) {
        if (s_isoc_active[i]) {
            return true;
        }
    }
    return false;
}

static void mark_fault(bool configuration_failure)
{
    s_faulted = true;
    __atomic_store_n(&s_accepting, false, __ATOMIC_RELEASE);
    s_streaming = false;
    s_configuring = false;
    s_stopping = true;
    if (configuration_failure) {
        s_config_failures++;
    } else {
        s_transfer_failures++;
    }
    lower_to_transition_priority();
}

static esp_err_t prepare_and_submit(usb_transfer_t *transfer)
{
    const int index = transfer_index(transfer);
    if (index < 0 || !s_device || !s_claimed || s_stopping) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t offset = 0u;
    for (int packet = 0; packet < transfer->num_isoc_packets; ++packet) {
        const uint16_t frames = flx4_uac_packetizer_next_frames(&s_packetizer);
        const size_t bytes =
            (size_t)frames * STREAM_CHANNELS * STREAM_BYTES_PER_SAMPLE;
        if (bytes > s_format.max_packet_size ||
            offset + bytes > transfer->data_buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        portENTER_CRITICAL(&s_mux);
        (void)controller_audio_ring_read(
            &s_ring, (int16_t *)&transfer->data_buffer[offset], frames, true);
        portEXIT_CRITICAL(&s_mux);
        transfer->isoc_packet_desc[packet].num_bytes = (int)bytes;
        offset += bytes;
    }

    transfer->device_handle = s_device;
    transfer->bEndpointAddress = s_format.endpoint_addr;
    transfer->num_bytes = (int)offset;
    s_isoc_active[index] = true;
    const esp_err_t rc = usb_host_transfer_submit(transfer);
    if (rc != ESP_OK) {
        s_isoc_active[index] = false;
    }
    return rc;
}

static void isoc_callback(usb_transfer_t *transfer)
{
    const int index = transfer_index(transfer);
    if (index >= 0) {
        s_isoc_active[index] = false;
    }
    if (!transfer) {
        return;
    }
    if (transfer->status == USB_TRANSFER_STATUS_CANCELED ||
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        s_device_gone = s_device_gone ||
                        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE;
        s_stopping = true;
        __atomic_store_n(&s_accepting, false, __ATOMIC_RELEASE);
        s_streaming = false;
        lower_to_transition_priority();
        return;
    }
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "isochronous status=%d", (int)transfer->status);
        mark_fault(false);
        return;
    }
    if (!s_stopping) {
        const esp_err_t rc = prepare_and_submit(transfer);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "isochronous resubmit: %s", esp_err_to_name(rc));
            mark_fault(false);
        }
    }
}

static esp_err_t submit_control_step(uint8_t step)
{
    usb_setup_packet_t *setup = (usb_setup_packet_t *)s_control->data_buffer;
    memset(setup, 0, sizeof(*setup));
    if (step == 1u) {
        setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                               USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                               USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
        setup->bRequest = USB_B_REQUEST_SET_INTERFACE;
        setup->wValue = s_format.alternate_setting;
        setup->wIndex = s_format.interface_num;
        setup->wLength = 0u;
        s_control->num_bytes = sizeof(*setup);
    } else {
        setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                               USB_BM_REQUEST_TYPE_TYPE_CLASS |
                               USB_BM_REQUEST_TYPE_RECIP_ENDPOINT;
        setup->bRequest = 0x01u;
        setup->wValue = 0x0100u;
        setup->wIndex = s_format.endpoint_addr;
        setup->wLength = 3u;
        uint8_t *rate = &s_control->data_buffer[sizeof(*setup)];
        rate[0] = 0x44u;
        rate[1] = 0xACu;
        rate[2] = 0x00u;
        s_control->num_bytes = sizeof(*setup) + 3u;
    }
    s_control_step = step;
    s_control->device_handle = s_device;
    s_control->bEndpointAddress = 0u;
    s_control_active = true;
    const esp_err_t rc = usb_host_transfer_submit_control(s_client, s_control);
    if (rc != ESP_OK) {
        s_control_active = false;
    }
    return rc;
}

static void control_callback(usb_transfer_t *transfer)
{
    s_control_active = false;
    if (!transfer) {
        return;
    }
    if (transfer->status == USB_TRANSFER_STATUS_CANCELED ||
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        s_device_gone = s_device_gone ||
                        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE;
        s_stopping = true;
        __atomic_store_n(&s_accepting, false, __ATOMIC_RELEASE);
        s_configuring = false;
        lower_to_transition_priority();
        return;
    }
    if (s_stopping) {
        return;
    }
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "control step %u status=%d", s_control_step,
                 (int)transfer->status);
        mark_fault(true);
        return;
    }
    if (s_control_step == 1u) {
        const esp_err_t rc = submit_control_step(2u);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "UAC SET_CUR submit: %s", esp_err_to_name(rc));
            mark_fault(true);
        }
        return;
    }

    bool all_submitted = true;
    for (unsigned i = 0u; i < STREAM_TRANSFER_COUNT; ++i) {
        const size_t bytes =
            (size_t)s_format.max_packet_size * STREAM_PACKETS_PER_TRANSFER;
        if (!s_isoc[i] && usb_host_transfer_alloc(
                bytes, STREAM_PACKETS_PER_TRANSFER, &s_isoc[i]) != ESP_OK) {
            all_submitted = false;
            break;
        }
        s_isoc[i]->callback = isoc_callback;
        if (prepare_and_submit(s_isoc[i]) != ESP_OK) {
            all_submitted = false;
            break;
        }
    }
    if (!all_submitted) {
        ESP_LOGW(TAG, "failed to prime UAC isochronous queue");
        mark_fault(true);
        return;
    }

    s_control_step = 0u;
    s_configuring = false;
    s_streaming = true;
    __atomic_store_n(&s_accepting, true, __ATOMIC_RELEASE);
    if (s_owner_task && s_active_priority > 0u) {
        vTaskPrioritySet(s_owner_task, s_active_priority);
    }
    ESP_LOGI(TAG,
             "FLX4 UAC ready intf=%u alt=%u ep=0x%02X 44100 Hz 4ch/16-bit",
             s_format.interface_num, s_format.alternate_setting,
             s_format.endpoint_addr);
}

esp_err_t controller_usb_audio_stream_start(
    usb_host_client_handle_t client,
    usb_device_handle_t device,
    const uint8_t *config_descriptor,
    size_t config_descriptor_length,
    TaskHandle_t owner_task,
    UBaseType_t active_priority,
    UBaseType_t transition_priority)
{
    if (!client || !device || !config_descriptor ||
        config_descriptor_length < 9u || !owner_task ||
        active_priority == 0u || transition_priority == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!controller_usb_audio_stream_is_quiesced()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!select_stream_format(config_descriptor, config_descriptor_length,
                              &s_format)) {
        s_config_failures++;
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_client = client;
    s_device = device;
    s_owner_task = owner_task;
    s_active_priority = active_priority;
    s_transition_priority = transition_priority;
    s_device_gone = false;
    s_flush_attempted = false;
    s_faulted = false;
    s_stopping = false;
    s_configuring = true;
    s_streaming = false;
    __atomic_store_n(&s_accepting, false, __ATOMIC_RELEASE);
    s_control_step = 0u;
    memset(s_isoc_active, 0, sizeof(s_isoc_active));
    memset(&s_resampler, 0, sizeof(s_resampler));
    flx4_uac_packetizer_init(&s_packetizer, STREAM_RATE_HZ,
                             STREAM_CHANNELS, STREAM_BYTES_PER_SAMPLE);
    if (!s_ring.samples) {
        if (!controller_audio_ring_init(&s_ring, s_ring_storage,
                                        STREAM_RING_FRAMES, STREAM_CHANNELS,
                                        STREAM_RATE_HZ)) {
            mark_fault(true);
            return ESP_FAIL;
        }
    } else {
        portENTER_CRITICAL(&s_mux);
        controller_audio_ring_reset(&s_ring, STREAM_RATE_HZ);
        portEXIT_CRITICAL(&s_mux);
    }

    esp_err_t rc = usb_host_interface_claim(
        client, device, s_format.interface_num, s_format.alternate_setting);
    if (rc != ESP_OK) {
        s_config_failures++;
        s_configuring = false;
        s_client = NULL;
        s_device = NULL;
        return rc;
    }
    s_claimed = true;

    rc = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 3u, 0,
                                 &s_control);
    if (rc == ESP_OK) {
        s_control->callback = control_callback;
        rc = submit_control_step(1u);
    }
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "UAC configuration start: %s", esp_err_to_name(rc));
        mark_fault(true);
        return rc;
    }
    return ESP_OK;
}

void controller_usb_audio_stream_request_stop(bool device_gone)
{
    __atomic_store_n(&s_accepting, false, __ATOMIC_RELEASE);
    s_streaming = false;
    s_configuring = false;
    s_stopping = s_claimed || s_control || s_control_active;
    s_device_gone = s_device_gone || device_gone;
    lower_to_transition_priority();
}

bool controller_usb_audio_stream_poll_cleanup(void)
{
    if (!s_stopping) {
        return controller_usb_audio_stream_is_quiesced();
    }
    if (!s_device_gone && s_claimed && s_device && has_active_isoc() &&
        !s_flush_attempted) {
        s_flush_attempted = true;
        const esp_err_t halt_rc =
            usb_host_endpoint_halt(s_device, s_format.endpoint_addr);
        if (halt_rc == ESP_OK || halt_rc == ESP_ERR_INVALID_STATE) {
            const esp_err_t flush_rc =
                usb_host_endpoint_flush(s_device, s_format.endpoint_addr);
            if (flush_rc != ESP_OK && flush_rc != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "UAC endpoint flush: %s",
                         esp_err_to_name(flush_rc));
            }
        } else {
            ESP_LOGW(TAG, "UAC endpoint halt: %s", esp_err_to_name(halt_rc));
        }
    }
    if (s_control_active || has_active_isoc()) {
        return false;
    }

    for (unsigned i = 0u; i < STREAM_TRANSFER_COUNT; ++i) {
        if (s_isoc[i]) {
            if (usb_host_transfer_free(s_isoc[i]) != ESP_OK) {
                return false;
            }
            s_isoc[i] = NULL;
        }
    }
    if (s_control) {
        if (usb_host_transfer_free(s_control) != ESP_OK) {
            return false;
        }
        s_control = NULL;
    }
    if (s_claimed) {
        const esp_err_t rc = usb_host_interface_release(
            s_client, s_device, s_format.interface_num);
        if (rc != ESP_OK) {
            return false;
        }
        s_claimed = false;
    }

    s_client = NULL;
    s_device = NULL;
    s_owner_task = NULL;
    s_control_step = 0u;
    s_configuring = false;
    s_streaming = false;
    s_stopping = false;
    s_device_gone = false;
    s_flush_attempted = false;
    memset(&s_format, 0, sizeof(s_format));
    memset(&s_resampler, 0, sizeof(s_resampler));
    portENTER_CRITICAL(&s_mux);
    controller_audio_ring_reset(&s_ring, STREAM_RATE_HZ);
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool controller_usb_audio_stream_is_quiesced(void)
{
    return !s_claimed && !s_control && !s_control_active &&
           !has_active_isoc();
}

esp_err_t controller_usb_audio_stream_write(const int16_t *master_samples,
                                            const int16_t *headphone_samples,
                                            size_t frame_count,
                                            uint32_t source_sample_rate)
{
    if ((!master_samples && !headphone_samples) || frame_count == 0u ||
        source_sample_rate < STREAM_RATE_HZ || source_sample_rate > 48000u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!__atomic_load_n(&s_accepting, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_resampler.source_rate != source_sample_rate ||
        s_resampler.target_rate != STREAM_RATE_HZ ||
        s_resampler.channels != STREAM_CHANNELS) {
        if (!controller_audio_resampler_init(&s_resampler, source_sample_rate,
                                             STREAM_RATE_HZ,
                                             STREAM_CHANNELS)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    int16_t input[RESAMPLE_INPUT_FRAMES * STREAM_CHANNELS];
    int16_t output[RESAMPLE_OUTPUT_FRAMES * STREAM_CHANNELS];
    bool dropped = false;
    while (frame_count > 0u) {
        const size_t chunk = frame_count > RESAMPLE_INPUT_FRAMES
                                 ? RESAMPLE_INPUT_FRAMES
                                 : frame_count;
        for (size_t i = 0u; i < chunk; ++i) {
            const int16_t ml = master_samples ? master_samples[i * 2u] >> 2 : 0;
            const int16_t mr = master_samples ? master_samples[i * 2u + 1u] >> 2 : 0;
            const int16_t hl = headphone_samples ? headphone_samples[i * 2u] >> 2 : ml;
            const int16_t hr = headphone_samples ? headphone_samples[i * 2u + 1u] >> 2 : mr;
            input[i * 4u] = ml;
            input[i * 4u + 1u] = mr;
            input[i * 4u + 2u] = hl;
            input[i * 4u + 3u] = hr;
        }
        const size_t output_frames = controller_audio_resampler_process(
            &s_resampler, input, chunk, output, RESAMPLE_OUTPUT_FRAMES);
        if (output_frames > 0u) {
            portENTER_CRITICAL(&s_mux);
            const uint64_t overrun_before = s_ring.overrun_frames;
            const uint32_t accepted = controller_audio_ring_write_clocked(
                &s_ring, output, (uint32_t)output_frames);
            dropped = dropped || s_ring.overrun_frames != overrun_before;
            portEXIT_CRITICAL(&s_mux);
            __atomic_add_fetch(&s_submitted_frames, accepted,
                               __ATOMIC_RELAXED);
        }
        if (master_samples) {
            master_samples += chunk * 2u;
        }
        if (headphone_samples) {
            headphone_samples += chunk * 2u;
        }
        frame_count -= chunk;
    }
    if (dropped) {
        __atomic_add_fetch(&s_dropped_blocks, 1u, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&s_submitted_blocks, 1u, __ATOMIC_RELAXED);
    return ESP_OK;
}

void controller_usb_audio_stream_get_stats(
    controller_usb_audio_stream_stats_t *out_stats)
{
    if (!out_stats) {
        return;
    }
    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->submitted_blocks =
        __atomic_load_n(&s_submitted_blocks, __ATOMIC_RELAXED);
    out_stats->dropped_blocks =
        __atomic_load_n(&s_dropped_blocks, __ATOMIC_RELAXED);
    out_stats->submitted_frames =
        __atomic_load_n(&s_submitted_frames, __ATOMIC_RELAXED);
    portENTER_CRITICAL(&s_mux);
    out_stats->ring_queued_frames = s_ring.queued_frames;
    out_stats->ring_capacity_frames = s_ring.frame_capacity;
    out_stats->ring_high_water_frames = s_ring.high_water_frames;
    out_stats->overrun_frames = s_ring.overrun_frames;
    out_stats->underrun_frames = s_ring.underrun_frames;
    out_stats->clock_trimmed_frames = s_ring.clock_trimmed_frames;
    out_stats->clock_duplicated_frames = s_ring.clock_duplicated_frames;
    portEXIT_CRITICAL(&s_mux);
    out_stats->config_failures = s_config_failures;
    out_stats->transfer_failures = s_transfer_failures;
    out_stats->claimed = s_claimed;
    out_stats->configuring = s_configuring;
    out_stats->streaming = s_streaming;
    out_stats->faulted = s_faulted;
}
