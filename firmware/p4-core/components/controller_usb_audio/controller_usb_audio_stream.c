/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_usb_audio_stream.h"

#include <string.h>

#include "controller_audio_resampler.h"
#include "controller_audio_ring.h"
#include "esp_log.h"
#include "flx4_uac_descriptors.h"
#include "flx4_uac_packetizer.h"
#include "service_log.h"

#define STREAM_RATE_HZ 44100u
#define STREAM_CHANNELS 4u
#define STREAM_BYTES_PER_SAMPLE 2u
#define STREAM_RING_FRAMES 2048u
#define STREAM_TRANSFER_COUNT 3u
#define STREAM_PACKETS_PER_TRANSFER 4
#define RESAMPLE_INPUT_FRAMES 128u
#define RESAMPLE_OUTPUT_FRAMES 142u
#if defined(CONFIG_PAJONIIIR_BOARD_JC1060) || defined(CONTROLLER_USB_AUDIO_PC_TEST)
#define STREAM_FIFO_BYTES 640u
#else
#define STREAM_FIFO_BYTES 400u
#endif

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
/* Admission and in-flight ownership share one atomic word. Cleanup never waits
 * at USB priority for the lower-priority output owner; it polls on later turns. */
#define WRITE_ACCEPTING 1u
#define WRITE_ACTIVE 2u
static uint32_t s_write_gate;
static void set_accepting(bool accepting)
{
    if (accepting) __atomic_fetch_or(&s_write_gate, WRITE_ACCEPTING, __ATOMIC_RELEASE);
    else __atomic_fetch_and(&s_write_gate, ~WRITE_ACCEPTING, __ATOMIC_ACQ_REL);
}
static void finish_write(void)
{
    __atomic_fetch_and(&s_write_gate, ~WRITE_ACTIVE, __ATOMIC_RELEASE);
}
static uint64_t s_submitted_blocks;
static uint64_t s_dropped_blocks;
static uint64_t s_submitted_frames;
static uint32_t s_config_failures;
static uint32_t s_transfer_failures;
static uint32_t s_packet_failures;
static uint64_t s_packet_lost_frames;
/* At most four detail records per boot. Never log successful packets or do
 * filesystem work at USB priority; service_log_event only enqueues a copy. */
static uint32_t s_packet_detail_records;
static TickType_t s_last_isoc_callback_tick;
static bool s_isoc_callback_seen;
/* Monotonic per-boot identity for each successfully primed UAC stream. */
static uint32_t s_stream_epoch;
static uint32_t s_target_rate = STREAM_RATE_HZ;
static uint8_t s_sample_bits;
static bool s_qualified_flx4 = true;
static bool s_consumer_paced;
static TickType_t s_control_started;
static bool s_control_expired;
static bool s_alt_zero_sent;

bool controller_usb_audio_stream_consumer_paced(void)
{ return __atomic_load_n(&s_consumer_paced, __ATOMIC_ACQUIRE); }
bool controller_usb_audio_stream_needs_recovery(void) { return s_control_expired; }

esp_err_t controller_usb_audio_stream_set_policy(bool qualified_flx4, bool consumer_paced)
{
    if (!controller_usb_audio_stream_is_quiesced()) return ESP_ERR_INVALID_STATE;
    s_qualified_flx4 = qualified_flx4;
    __atomic_store_n(&s_consumer_paced, consumer_paced, __ATOMIC_RELEASE);
    return ESP_OK;
}

/* Caller has stopped the producer; claim the same gate as a write. */
esp_err_t controller_usb_audio_stream_set_pacing(bool consumer_paced)
{
    uint32_t expected = WRITE_ACCEPTING;
    if (!__atomic_compare_exchange_n(&s_write_gate, &expected, WRITE_ACCEPTING | WRITE_ACTIVE,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (expected != 0u) return ESP_ERR_INVALID_STATE;
        __atomic_store_n(&s_consumer_paced, consumer_paced, __ATOMIC_RELEASE);
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_mux);
    controller_audio_ring_reset(&s_ring, s_target_rate);
    memset(&s_resampler, 0, sizeof(s_resampler));
    portEXIT_CRITICAL(&s_mux);
    __atomic_store_n(&s_consumer_paced, consumer_paced, __ATOMIC_RELEASE);
    __atomic_add_fetch(&s_stream_epoch, 1u, __ATOMIC_RELEASE);
    finish_write();
    return ESP_OK;
}

esp_err_t controller_usb_audio_stream_wait_room(uint32_t source_frames,
                                               uint32_t source_rate, uint32_t timeout_ms)
{
    if (!source_rate || !source_frames || source_frames > STREAM_RING_FRAMES) return ESP_ERR_INVALID_ARG;
    const uint32_t epoch = __atomic_load_n(&s_stream_epoch, __ATOMIC_ACQUIRE);
    const uint32_t rate = __atomic_load_n(&s_target_rate, __ATOMIC_ACQUIRE);
    size_t needed = controller_audio_resampler_output_bound(source_rate, rate, source_frames);
    const TickType_t started = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    if (!timeout && timeout_ms) timeout = 1u;
    for (;;) {
        if (!(__atomic_load_n(&s_write_gate, __ATOMIC_ACQUIRE) & WRITE_ACCEPTING) ||
            epoch != __atomic_load_n(&s_stream_epoch, __ATOMIC_ACQUIRE)) return ESP_ERR_INVALID_STATE;
        portENTER_CRITICAL(&s_mux);
        bool room = needed <= STREAM_RING_FRAMES / 2u &&
                    s_ring.queued_frames <= STREAM_RING_FRAMES / 2u - needed;
        portEXIT_CRITICAL(&s_mux);
        if (room) return ESP_OK;
        if ((TickType_t)(xTaskGetTickCount() - started) >= timeout) return ESP_ERR_TIMEOUT;
        vTaskDelay(1u); /* USB consumption, not wall-clock audio pacing. */
    }
}

static void lower_to_transition_priority(void)
{
    if (s_owner_task && s_transition_priority > 0u) {
        vTaskPrioritySet(s_owner_task, s_transition_priority);
    }
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
    const uint32_t rates[] = {44100u, 48000u};
    for (unsigned rate = 0; rate < (s_qualified_flx4 ? 1u : 2u); ++rate) {
        for (unsigned width = 0; width < (s_qualified_flx4 ? 1u : 2u); ++width) {
            for (uint8_t i = 0; i < parsed.format_count; ++i) {
                const flx4_uac_playback_format_t *candidate = &parsed.formats[i];
                if (candidate->channels == STREAM_CHANNELS &&
                    candidate->max_packet_size <= STREAM_FIFO_BYTES &&
                    (s_qualified_flx4 || (candidate->pcm_explicit && candidate->sync_type >= 2u)) &&
                    candidate->bits_per_sample == (width ? 24u : 16u) &&
                    flx4_uac_format_supports_rate(candidate, rates[rate])) {
                    *out = *candidate;
                    /* Preserve the qualified FLX4 SET_CUR sequence. Generic
                     * endpoints opt in only via their UAC1 CS endpoint flag. */
                    if (s_qualified_flx4) out->frequency_control = true;
                    __atomic_store_n(&s_target_rate, rates[rate], __ATOMIC_RELEASE);
                    __atomic_store_n(&s_sample_bits, candidate->bits_per_sample, __ATOMIC_RELEASE);
                    return true;
                }
            }
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
    set_accepting(false);
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
            (size_t)frames * STREAM_CHANNELS * s_format.bytes_per_sample;
        if (bytes > s_format.max_packet_size ||
            offset + bytes > transfer->data_buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        int16_t pcm[48u * STREAM_CHANNELS];
        int16_t *samples = s_format.bytes_per_sample == 2u ?
            (int16_t *)&transfer->data_buffer[offset] : pcm;
        portENTER_CRITICAL(&s_mux);
        (void)controller_audio_ring_read(&s_ring, samples, frames, true);
        portEXIT_CRITICAL(&s_mux);
        if (s_format.bytes_per_sample == 3u &&
            !controller_uac_pack_pcm(&transfer->data_buffer[offset], bytes, pcm, frames, 3u))
            return ESP_ERR_INVALID_SIZE;
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
        set_accepting(false);
        s_streaming = false;
        lower_to_transition_priority();
        return;
    }
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "isochronous status=%d", (int)transfer->status);
        mark_fault(false);
        return;
    }
    const TickType_t callback_tick = xTaskGetTickCount();
    const uint32_t callback_gap_ticks = s_isoc_callback_seen
        ? (uint32_t)(callback_tick - s_last_isoc_callback_tick) : 0u;
    s_last_isoc_callback_tick = callback_tick;
    s_isoc_callback_seen = true;
    /* HCD reports a completed URB even if individual ISO packets were skipped
     * or failed. Count loss before prepare_and_submit overwrites descriptors.
     * Isolated loss does not restart USB; terminal URB faults retain the bounded
     * recovery policy above. */
    for (int i = 0; i < transfer->num_isoc_packets; ++i) {
        const int wanted = transfer->isoc_packet_desc[i].num_bytes;
        const int actual = transfer->isoc_packet_desc[i].actual_num_bytes;
        const bool completed = transfer->isoc_packet_desc[i].status ==
                               USB_TRANSFER_STATUS_COMPLETED;
        if (!completed || actual != wanted) {
            if (s_packet_detail_records < 4u) {
                ++s_packet_detail_records;
                service_log_event(SERVICE_LOG_UAC_PACKET_FAULT, SERVICE_LOG_WARN,
                    4u, (uint32_t)transfer->isoc_packet_desc[i].status,
                    (uint32_t)actual, (uint32_t)wanted, callback_gap_ticks,
                    "status/actual/wanted/callback gap ticks");
            }
            __atomic_add_fetch(&s_packet_failures, 1u, __ATOMIC_RELAXED);
            const unsigned missing = !completed || actual < 0 || actual > wanted
                ? (unsigned)wanted : (unsigned)(wanted - actual);
            __atomic_add_fetch(&s_packet_lost_frames,
                (missing + STREAM_CHANNELS * s_format.bytes_per_sample - 1u) /
                    (STREAM_CHANNELS * s_format.bytes_per_sample), __ATOMIC_RELAXED);
        }
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
    if (step == 1u || step == 3u) {
        setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                               USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                               USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
        setup->bRequest = USB_B_REQUEST_SET_INTERFACE;
        setup->wValue = step == 3u ? 0u : s_format.alternate_setting;
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
        rate[0] = (uint8_t)s_target_rate;
        rate[1] = (uint8_t)(s_target_rate >> 8);
        rate[2] = (uint8_t)(s_target_rate >> 16);
        s_control->num_bytes = sizeof(*setup) + 3u;
    }
    s_control_step = step;
    s_control->device_handle = s_device;
    s_control->bEndpointAddress = 0u;
    s_control_active = true;
    s_control_started = xTaskGetTickCount();
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
        set_accepting(false);
        s_configuring = false;
        lower_to_transition_priority();
        return;
    }
    if (s_stopping) {
        if (s_control_step == 3u && transfer->status != USB_TRANSFER_STATUS_COMPLETED)
            s_control_expired = true;
        return;
    }
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "control step %u status=%d", s_control_step,
                 (int)transfer->status);
        mark_fault(true);
        return;
    }
    if (s_control_step == 1u && s_format.frequency_control) {
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
    __atomic_add_fetch(&s_stream_epoch, 1u, __ATOMIC_RELEASE);
    s_isoc_callback_seen = false;
    s_streaming = true;
    set_accepting(true);
    if (s_owner_task && s_active_priority > 0u) {
        vTaskPrioritySet(s_owner_task, s_active_priority);
    }
    ESP_LOGI(TAG,
             "UAC ready intf=%u alt=%u ep=0x%02X %lu Hz 4ch/%u-bit",
             s_format.interface_num, s_format.alternate_setting,
             s_format.endpoint_addr, (unsigned long)s_target_rate, s_format.bits_per_sample);
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
    s_control_expired = false;
    s_alt_zero_sent = false;
    s_faulted = false;
    s_stopping = false;
    s_configuring = true;
    s_streaming = false;
    set_accepting(false);
    s_control_step = 0u;
    memset(s_isoc_active, 0, sizeof(s_isoc_active));
    memset(&s_resampler, 0, sizeof(s_resampler));
    flx4_uac_packetizer_init(&s_packetizer, s_target_rate,
                             STREAM_CHANNELS, s_format.bytes_per_sample);
    if (!s_ring.samples) {
        if (!controller_audio_ring_init(&s_ring, s_ring_storage,
                                        STREAM_RING_FRAMES, STREAM_CHANNELS,
                                        s_target_rate)) {
            mark_fault(true);
            return ESP_FAIL;
        }
    } else {
        portENTER_CRITICAL(&s_mux);
        controller_audio_ring_reset(&s_ring, s_target_rate);
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
    set_accepting(false);
    s_streaming = false;
    s_configuring = false;
    s_stopping = s_claimed || s_control || s_control_active;
    s_device_gone = s_device_gone || device_gone;
    lower_to_transition_priority();
}

bool controller_usb_audio_stream_poll_cleanup(void)
{
    if (s_control_active && !s_control_expired &&
        (TickType_t)(xTaskGetTickCount() - s_control_started) >= pdMS_TO_TICKS(1000u)) {
        s_control_expired = true;
        mark_fault(true); /* Retain USB ownership until cancellation callback. */
    }
    if (!s_stopping) return controller_usb_audio_stream_is_quiesced();
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
    if (s_control_active || has_active_isoc() ||
        (__atomic_load_n(&s_write_gate, __ATOMIC_ACQUIRE) & WRITE_ACTIVE)) {
        return false;
    }

    if (!s_device_gone && s_claimed && !s_control_expired && !s_alt_zero_sent) {
        s_alt_zero_sent = true;
        if (submit_control_step(3u) == ESP_OK) return false;
        s_control_expired = true; /* Owner requests bounded root recovery. */
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
    controller_audio_ring_reset(&s_ring, s_target_rate);
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool controller_usb_audio_stream_is_quiesced(void)
{
    return !s_claimed && !s_control && !s_control_active &&
           !has_active_isoc() &&
           __atomic_load_n(&s_write_gate, __ATOMIC_ACQUIRE) == 0u;
}

esp_err_t controller_usb_audio_stream_write(const int16_t *master_samples,
                                            const int16_t *headphone_samples,
                                            size_t frame_count,
                                            uint32_t source_sample_rate)
{
    if ((!master_samples && !headphone_samples) || frame_count == 0u || frame_count > UINT32_MAX ||
        (source_sample_rate != 44100u && source_sample_rate != 48000u)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t expected_gate = WRITE_ACCEPTING;
    if (!__atomic_compare_exchange_n(&s_write_gate, &expected_gate,
            WRITE_ACCEPTING | WRITE_ACTIVE, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (controller_usb_audio_stream_consumer_paced()) {
        esp_err_t wait_rc = controller_usb_audio_stream_wait_room((uint32_t)frame_count,
                                                                source_sample_rate, 100u);
        if (wait_rc != ESP_OK) { finish_write(); return wait_rc; }
    }
    if (s_resampler.source_rate != source_sample_rate ||
        s_resampler.target_rate != s_target_rate ||
        s_resampler.channels != STREAM_CHANNELS) {
        if (!controller_audio_resampler_init(&s_resampler, source_sample_rate,
                                             s_target_rate,
                                             STREAM_CHANNELS)) {
            finish_write();
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
        const unsigned attenuation = s_qualified_flx4 && !controller_usb_audio_stream_consumer_paced() ? 2u : 0u;
        for (size_t i = 0u; i < chunk; ++i) {
            const int16_t ml = master_samples ? master_samples[i * 2u] >> attenuation : 0;
            const int16_t mr = master_samples ? master_samples[i * 2u + 1u] >> attenuation : 0;
            const int16_t hl = headphone_samples ? headphone_samples[i * 2u] >> attenuation : ml;
            const int16_t hr = headphone_samples ? headphone_samples[i * 2u + 1u] >> attenuation : mr;
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
            const uint32_t accepted = controller_usb_audio_stream_consumer_paced() ?
                controller_audio_ring_write(&s_ring, output, (uint32_t)output_frames) :
                controller_audio_ring_write_clocked(&s_ring, output, (uint32_t)output_frames);
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
    const bool canceled = controller_usb_audio_stream_consumer_paced() &&
        !(__atomic_load_n(&s_write_gate, __ATOMIC_ACQUIRE) & WRITE_ACCEPTING);
    finish_write();
    return canceled ? ESP_ERR_INVALID_STATE : ESP_OK;
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
    out_stats->packet_failures = __atomic_load_n(&s_packet_failures, __ATOMIC_RELAXED);
    out_stats->packet_lost_frames = __atomic_load_n(&s_packet_lost_frames, __ATOMIC_RELAXED);
    out_stats->stream_epoch = __atomic_load_n(&s_stream_epoch, __ATOMIC_ACQUIRE);
    out_stats->claimed = s_claimed;
    out_stats->configuring = s_configuring;
    out_stats->streaming = s_streaming;
    out_stats->faulted = s_faulted;
    out_stats->sample_rate = __atomic_load_n(&s_target_rate, __ATOMIC_ACQUIRE);
    out_stats->bits_per_sample = __atomic_load_n(&s_sample_bits, __ATOMIC_ACQUIRE);
    out_stats->consumer_paced = controller_usb_audio_stream_consumer_paced();
}
