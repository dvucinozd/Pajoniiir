/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_usb_audio_stream.h"

#include <string.h>

#include "controller_audio_resampler.h"
#include "controller_audio_ring.h"
#include "controller_uac2.h"
#include "esp_log.h"
#include "flx4_uac_descriptors.h"
#include "flx4_uac_packetizer.h"
#include "service_log.h"

#define STREAM_RATE_HZ 44100u
#define STREAM_CHANNELS 4u
/* FS 48-kHz feedback permits +5% clock correction: ceil(48 * 1.05).
 * Endpoint and transfer byte bounds still determine each packet's limit. */
#define STREAM_MAX_PACKET_FRAMES 51u
#define STREAM_BYTES_PER_SAMPLE 2u
#define STREAM_RING_FRAMES 2048u
#define STREAM_TRANSFER_COUNT 3u
#define STREAM_PACKETS_PER_TRANSFER 4
/* Pinned HCD has two hardware buffers and 64 microframe descriptors, with
 * three reserved for scheduling margin. 56 packets gives each HS buffer 7 ms
 * of work (14 ms double-buffer runway) through dense MIDI callback bursts.
 * FS remains four 1-ms packets. Keep below 64 - 3, never allocate unboundedly. */
#define STREAM_HS_PACKETS_PER_TRANSFER 56
#define RESAMPLE_INPUT_FRAMES 128u
#define RESAMPLE_OUTPUT_FRAMES 142u
/* Packet admission follows the controller's physical root FIFO, not device
 * speed: a Full Speed Hercules can be connected to M3's HS controller root.
 * Host tests select real board/root policies instead of PC-only widening. */
#if defined(CONFIG_PAJONIIIR_BOARD_JC4880) || defined(CONFIG_PAJONIIIR_BOARD_JC1060) || (CONFIG_BSP_M3_HS_CONTROLLER && CONFIG_CONTROLLER_USB_HS_AUDIO_DIAGNOSTIC)
#define STREAM_CONTROLLER_FIFO_BYTES 640u
#elif CONFIG_BSP_M3_HS_CONTROLLER
#define STREAM_CONTROLLER_FIFO_BYTES 512u
#else
#define STREAM_CONTROLLER_FIFO_BYTES 400u
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
static usb_transfer_t *s_feedback;
static bool s_feedback_active;
static bool s_high_speed;
static uint32_t s_feedback_q16, s_feedback_phase;
static uint32_t s_feedback_updates, s_feedback_invalid;
static TickType_t s_feedback_tick;
static uint16_t s_clock_range_bytes;
/* Rate changes may complete after SET_CUR's status stage. Never sleep in the
 * USB callback or stream at an unverified rate: poll within one bounded window. */
static TickType_t s_clock_change_tick, s_clock_retry_tick;
static uint8_t s_clock_retry_step;
static uint32_t s_clock_readback_retries, s_clock_reported_rate;
static uint8_t s_prime_fault_stage, s_prime_fault_index;
static int32_t s_prime_fault_code;
static bool s_clock_44100_supported;
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
static bool s_configuration_fault;
static uint8_t s_fault_control_step;
static int32_t s_last_control_status = -1, s_last_control_bytes = -1;
static uint32_t s_last_control_word;
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
/* At most four fault pairs (eight records) per boot. Never log successful packets or do
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
static uint8_t s_wire_format = CONTROLLER_UAC_WIRE_DESCRIPTOR;
static TickType_t s_control_started;
static bool s_control_expired;
static bool s_alt_zero_sent;

/* HS DMA buffers must survive a stopped-deck reconnect: tracks can consume
 * the large contiguous internal-heap blocks after the initial attachment.
 * Retain only the three idle URBs, never a device/interface claim or an active
 * transfer. Immutable packet count/capacity are checked at the next start. FS
 * keeps its existing allocation/free lifecycle. */
static bool release_idle_isoc(unsigned index)
{
    if (!s_isoc[index]) return true;
    if (usb_host_transfer_free(s_isoc[index]) != ESP_OK) return false;
    s_isoc[index] = NULL;
    return true;
}

static void detach_idle_isoc(usb_transfer_t *transfer)
{
    transfer->device_handle = NULL;
    transfer->bEndpointAddress = 0;
    transfer->num_bytes = transfer->actual_num_bytes = 0;
    transfer->status = USB_TRANSFER_STATUS_COMPLETED;
    for (int p = 0; p < transfer->num_isoc_packets; ++p) {
        transfer->isoc_packet_desc[p].num_bytes = 0;
        transfer->isoc_packet_desc[p].actual_num_bytes = 0;
        transfer->isoc_packet_desc[p].status = USB_TRANSFER_STATUS_COMPLETED;
    }
}

bool controller_usb_audio_stream_consumer_paced(void)
{ return __atomic_load_n(&s_consumer_paced, __ATOMIC_ACQUIRE); }
bool controller_usb_audio_stream_needs_recovery(void) { return s_control_expired; }

esp_err_t controller_usb_audio_stream_set_wire_format(controller_uac_wire_format_t format)
{
    if (format != CONTROLLER_UAC_WIRE_DESCRIPTOR && format != CONTROLLER_UAC_WIRE_S16LE)
        return ESP_ERR_INVALID_ARG;
    __atomic_store_n(&s_wire_format, (uint8_t)format, __ATOMIC_RELEASE);
    return ESP_OK;
}

/* Bytes per sample actually sent; differs from the descriptor only for S16LE. */
static uint8_t wire_sample_bytes(void)
{
    if (!s_format.uac2 && s_format.bytes_per_sample == 3u &&
        __atomic_load_n(&s_wire_format, __ATOMIC_ACQUIRE) == CONTROLLER_UAC_WIRE_S16LE)
        return 2u;
    return s_format.bytes_per_sample;
}

esp_err_t controller_usb_audio_stream_set_policy(bool qualified_flx4, bool consumer_paced,
                                                 bool high_speed)
{
    if (!controller_usb_audio_stream_is_quiesced()) return ESP_ERR_INVALID_STATE;
    s_qualified_flx4 = qualified_flx4;
    s_high_speed = high_speed;
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
    if (s_high_speed) {
        if (s_qualified_flx4) return false;
        if (controller_uac2_parse(descriptor, descriptor_length, out)) {
            out->service_interval_us = 125u;
            __atomic_store_n(&s_target_rate, 48000u, __ATOMIC_RELEASE);
            __atomic_store_n(&s_sample_bits, out->bits_per_sample, __ATOMIC_RELEASE);
            return true;
        }
    }
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
                    candidate->max_packet_size <= STREAM_CONTROLLER_FIFO_BYTES &&
                    (s_qualified_flx4 ? !candidate->requires_feedback :
                        (candidate->pcm_explicit && (candidate->sync_type >= 2u ||
                                                   candidate->requires_feedback))) &&
                    candidate->bits_per_sample == (width ? 24u : 16u) &&
                    flx4_uac_format_supports_bus_rate(candidate, rates[rate], s_high_speed)) {
                    *out = *candidate;
                    out->service_interval_us = 1000u;
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

bool controller_usb_audio_stream_available(void)
{
    return (__atomic_load_n(&s_write_gate, __ATOMIC_ACQUIRE) & WRITE_ACCEPTING) != 0u;
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
    if (s_feedback_active) return true;
    for (unsigned i = 0u; i < STREAM_TRANSFER_COUNT; ++i) {
        if (s_isoc_active[i]) {
            return true;
        }
    }
    return false;
}

static void mark_fault(bool configuration_failure)
{
    if (!s_faulted) {
        s_configuration_fault = configuration_failure;
        s_fault_control_step = s_control_step;
    }
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

static void mark_prime_fault(uint8_t stage, uint8_t index, esp_err_t rc)
{
    s_prime_fault_stage = stage;
    s_prime_fault_index = index;
    s_prime_fault_code = rc;
    ESP_LOGW(TAG, "UAC prime stage=%u index=%u error=%s", stage, index, esp_err_to_name(rc));
    mark_fault(true);
}

static esp_err_t prepare_and_submit(usb_transfer_t *transfer)
{
    const int index = transfer_index(transfer);
    if (index < 0 || !s_device || !s_claimed || s_stopping) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t sample_bytes = wire_sample_bytes();
    size_t offset = 0u;
    for (int packet = 0; packet < transfer->num_isoc_packets; ++packet) {
        uint16_t frames;
        if (s_format.uac2 || s_format.requires_feedback) {
            s_feedback_phase += s_feedback_q16;
            frames = (uint16_t)(s_feedback_phase >> 16);
            s_feedback_phase &= 0xffffu;
        } else frames = flx4_uac_packetizer_next_frames(&s_packetizer);
        const size_t bytes = (size_t)frames * STREAM_CHANNELS * sample_bytes;
        if (frames > STREAM_MAX_PACKET_FRAMES || bytes > s_format.max_packet_size ||
            offset + bytes > transfer->data_buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        int16_t pcm[STREAM_MAX_PACKET_FRAMES * STREAM_CHANNELS];
        int16_t *samples = sample_bytes == 2u ?
            (int16_t *)&transfer->data_buffer[offset] : pcm;
        portENTER_CRITICAL(&s_mux);
        (void)controller_audio_ring_read(&s_ring, samples, frames, true);
        portEXIT_CRITICAL(&s_mux);
        if (s_format.uac2 && sample_bytes != 2u &&
            !controller_uac2_pack_pcm(&transfer->data_buffer[offset], bytes, pcm, frames,
                                      s_format.bytes_per_sample, s_format.bits_per_sample))
            return ESP_ERR_INVALID_SIZE;
        if (!s_format.uac2 && sample_bytes == 3u &&
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
                service_log_event(SERVICE_LOG_UAC_PACKET_FAULT, SERVICE_LOG_WARN,
                    4u, (uint32_t)i, (uint32_t)transfer->num_isoc_packets,
                    (uint32_t)index, callback_gap_ticks,
                    "packet index/count/transfer/callback gap ticks");
            }
            __atomic_add_fetch(&s_packet_failures, 1u, __ATOMIC_RELAXED);
            const unsigned missing = !completed || actual < 0 || actual > wanted
                ? (unsigned)wanted : (unsigned)(wanted - actual);
            const unsigned frame_bytes = STREAM_CHANNELS * wire_sample_bytes();
            __atomic_add_fetch(&s_packet_lost_frames,
                (missing + frame_bytes - 1u) / frame_bytes, __ATOMIC_RELAXED);
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
    } else if (step >= 4u && step <= 8u) {
        setup->bmRequestType = USB_BM_REQUEST_TYPE_TYPE_CLASS |
                               USB_BM_REQUEST_TYPE_RECIP_INTERFACE |
                               (step == 5u ? 0u : 0x80u);
        setup->bRequest = (step == 4u || step == 7u) ? 2u : 1u; /* RANGE / CUR */
        setup->wValue = step == 8u ? 0x0200u : 0x0100u; /* Clock validity / frequency */
        setup->wIndex = (uint16_t)s_format.clock_source << 8 | s_format.control_interface;
        setup->wLength = step == 4u ? 2u : step == 7u ? s_clock_range_bytes : step == 8u ? 1u : 4u;
        uint8_t *rate = &s_control->data_buffer[sizeof(*setup)];
        memset(rate, 0, setup->wLength);
        if (step == 5u) for (unsigned i = 0; i < 4; ++i)
            rate[i] = (uint8_t)(s_target_rate >> (8u * i));
        s_control->num_bytes = sizeof(*setup) + setup->wLength;
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

static esp_err_t submit_feedback(void)
{
    s_feedback->device_handle = s_device;
    s_feedback->bEndpointAddress = s_format.feedback_endpoint;
    const int bytes = s_format.uac2 ? 4 : 3;
    s_feedback->num_bytes = bytes;
    s_feedback->isoc_packet_desc[0].num_bytes = bytes;
    s_feedback_active = true;
    esp_err_t rc = usb_host_transfer_submit(s_feedback);
    if (rc != ESP_OK) s_feedback_active = false;
    return rc;
}

/* Only called before stream admission, with no audio transfers in flight.
 * MAIN keeps its board-owned rate; the existing resampler targets this clock. */
static void configure_uac2_rate(uint32_t rate)
{
    __atomic_store_n(&s_target_rate, rate, __ATOMIC_RELEASE);
    s_feedback_q16 = (uint32_t)(((uint64_t)rate << 16) / 8000u);
    s_feedback_phase = 0;
    flx4_uac_packetizer_init(&s_packetizer, rate, STREAM_CHANNELS, s_format.bytes_per_sample);
    portENTER_CRITICAL(&s_mux);
    controller_audio_ring_reset(&s_ring, rate);
    portEXIT_CRITICAL(&s_mux);
}

static bool try_uac2_rate_fallback(void)
{
    if (s_control_step != 6u || s_target_rate != 48000u ||
        !s_clock_44100_supported || s_clock_reported_rate != 44100u) return false;
    /* One explicit fallback attempt, still requiring SET/CUR and validity.
     * Never relabel 48-kHz packets as a 44.1-kHz device clock. */
    configure_uac2_rate(44100u);
    if (submit_control_step(5u) != ESP_OK) mark_fault(true);
    return true;
}

bool controller_usb_audio_stream_fault_isolatable(void)
{
    return s_high_speed && s_faulted && s_configuration_fault &&
           !s_control_expired && !s_device_gone;
}

static void feedback_callback(usb_transfer_t *transfer)
{
    s_feedback_active = false;
    if (transfer->status == USB_TRANSFER_STATUS_CANCELED ||
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        controller_usb_audio_stream_request_stop(
            transfer->status == USB_TRANSFER_STATUS_NO_DEVICE);
        return;
    }
    if (s_stopping) return;
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        mark_fault(false);
        return;
    }
    uint32_t value = 0;
    const int bytes = transfer->isoc_packet_desc[0].actual_num_bytes;
    const bool completed = transfer->isoc_packet_desc[0].status == USB_TRANSFER_STATUS_COMPLETED;
    const bool valid = completed && (s_format.uac2 ?
        (bytes == 4 && controller_uac2_feedback(transfer->data_buffer, 4, s_target_rate, &value)) :
        (bytes == 3 && flx4_uac_feedback_q16(&s_format, transfer->data_buffer, 3, s_target_rate, &value)));
    if (valid) {
        s_feedback_q16 = value;
        s_feedback_tick = xTaskGetTickCount();
        __atomic_add_fetch(&s_feedback_updates, 1u, __ATOMIC_RELAXED);
    } else if (!(completed && bytes == 0 && !s_format.uac2)) {
        /* FS feedback may have no new value between its bRefresh periods. */
        __atomic_add_fetch(&s_feedback_invalid, 1u, __ATOMIC_RELAXED);
    }
    if (submit_feedback() != ESP_OK) mark_fault(false);
}

static void control_callback(usb_transfer_t *transfer)
{
    s_control_active = false;
    if (!transfer) {
        return;
    }
    if (!s_stopping) {
        s_last_control_status = (int32_t)transfer->status;
        s_last_control_bytes = transfer->actual_num_bytes - (int)sizeof(usb_setup_packet_t);
        s_last_control_word = 0;
        if (s_last_control_bytes > 0 && transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
            const unsigned bytes = s_last_control_bytes > 4 ? 4u : (unsigned)s_last_control_bytes;
            for (unsigned i = 0; i < bytes; ++i)
                s_last_control_word |= (uint32_t)transfer->data_buffer[sizeof(usb_setup_packet_t) + i] << (8u * i);
        }
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
    if (s_control_step >= 4u && s_control_step <= 8u) {
        const uint8_t *payload = transfer->data_buffer + sizeof(usb_setup_packet_t);
        const int actual = transfer->actual_num_bytes - (int)sizeof(usb_setup_packet_t);
        uint8_t next = s_control_step == 4u ? 7u : s_control_step == 7u ? 5u :
                       s_control_step == 5u ? 6u : s_control_step == 6u ? 8u : 1u;
        bool valid = actual >= 0;
        if (s_control_step == 4u) {
            unsigned count = actual == 2 ? payload[0] | (unsigned)payload[1] << 8 : 0u;
            valid = count > 0 && count <= 8;
            if (valid) s_clock_range_bytes = (uint16_t)(2u + 12u * count);
        } else if (s_control_step == 7u) {
            s_clock_44100_supported = valid && controller_uac2_range_supports(payload, (size_t)actual, 44100u);
            valid = valid && controller_uac2_range_supports(payload, (size_t)actual, s_target_rate);
            if (!valid && s_clock_44100_supported) {
                configure_uac2_rate(44100u);
                valid = true;
            }
        }
        else if (s_control_step == 6u) {
            if (actual == 4) s_clock_reported_rate = s_last_control_word;
            valid = actual == 4 && payload[0] == (uint8_t)s_target_rate &&
                payload[1] == (uint8_t)(s_target_rate >> 8) &&
                payload[2] == (uint8_t)(s_target_rate >> 16) &&
                payload[3] == (uint8_t)(s_target_rate >> 24);
        }
        else if (s_control_step == 8u) valid = actual == 1 && payload[0] == 1u;
        else valid = actual == 4;
        if (s_control_step == 5u && valid) s_clock_change_tick = xTaskGetTickCount();
        if (!valid && ((s_control_step == 6u && actual == 4) ||
                      (s_control_step == 8u && actual == 1 && payload[0] == 0u)) &&
            (TickType_t)(xTaskGetTickCount() - s_clock_change_tick) < pdMS_TO_TICKS(500u)) {
            s_clock_retry_step = s_control_step;
            s_clock_retry_tick = xTaskGetTickCount();
            return;
        }
        if (!valid && actual == 4 && try_uac2_rate_fallback()) return;
        if (!valid || submit_control_step(next) != ESP_OK) mark_fault(true);
        return;
    }
    if (s_control_step == 1u && s_format.frequency_control && !s_format.uac2) {
        const esp_err_t rc = submit_control_step(2u);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "UAC SET_CUR submit: %s", esp_err_to_name(rc));
            mark_fault(true);
        }
        return;
    }

    bool all_submitted = true;
    if (s_format.uac2 || s_format.requires_feedback) {
        const esp_err_t alloc_rc = usb_host_transfer_alloc(4u, 1, &s_feedback);
        if (alloc_rc != ESP_OK) {
            mark_prime_fault(1u, 0u, alloc_rc);
            return;
        }
        s_feedback->callback = feedback_callback;
        s_feedback_tick = xTaskGetTickCount();
        const esp_err_t submit_rc = submit_feedback();
        if (submit_rc != ESP_OK) {
            mark_prime_fault(2u, 0u, submit_rc);
            return;
        }
    }
    const int packets = s_format.uac2 ? STREAM_HS_PACKETS_PER_TRANSFER : STREAM_PACKETS_PER_TRANSFER;
    for (unsigned i = 0u; i < STREAM_TRANSFER_COUNT; ++i) {
        const size_t bytes =
            (size_t)s_format.max_packet_size * packets;
        if (!s_isoc[i]) {
            const esp_err_t rc = usb_host_transfer_alloc(bytes, packets, &s_isoc[i]);
            if (rc != ESP_OK) {
                mark_prime_fault(3u, (uint8_t)i, rc);
                all_submitted = false;
                break;
            }
        }
        s_isoc[i]->callback = isoc_callback;
        const esp_err_t submit_rc = prepare_and_submit(s_isoc[i]);
        if (submit_rc != ESP_OK) {
            mark_prime_fault(4u, (uint8_t)i, submit_rc);
            all_submitted = false;
            break;
        }
    }
    if (!all_submitted) {
        ESP_LOGW(TAG, "failed to prime UAC isochronous queue");
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

    const int packets = s_format.uac2 ? STREAM_HS_PACKETS_PER_TRANSFER : STREAM_PACKETS_PER_TRANSFER;
    const size_t bytes = (size_t)s_format.max_packet_size * packets;
    for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) {
        if (s_isoc[i] && (!s_format.uac2 ||
            s_isoc[i]->num_isoc_packets != packets || s_isoc[i]->data_buffer_size < bytes)) {
            if (!release_idle_isoc(i)) return ESP_ERR_INVALID_STATE;
        }
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
    s_configuration_fault = false;
    s_fault_control_step = 0;
    s_last_control_status = -1;
    s_last_control_bytes = -1;
    s_last_control_word = 0;
    s_stopping = false;
    s_configuring = true;
    s_streaming = false;
    set_accepting(false);
    s_control_step = 0u;
    s_feedback_phase = 0u;
    s_feedback_q16 = (uint32_t)(((uint64_t)s_target_rate << 16u) /
                              (1000000u / s_format.service_interval_us));
    s_feedback_active = false;
    s_clock_range_bytes = 0;
    s_clock_44100_supported = false;
    s_clock_retry_step = 0;
    s_clock_change_tick = xTaskGetTickCount();
    s_clock_readback_retries = 0;
    s_clock_reported_rate = 0;
    s_prime_fault_stage = s_prime_fault_index = 0;
    s_prime_fault_code = ESP_OK;
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
        /* Claim allocates the actual HCD endpoints. Preserve its rejection
         * separately from asynchronous priming faults; MIDI stays available. */
        s_prime_fault_stage = 5u;
        s_prime_fault_index = s_format.interface_num;
        s_prime_fault_code = rc;
        s_configuring = false;
        s_client = NULL;
        s_device = NULL;
        return rc;
    }
    s_claimed = true;

    rc = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + (s_format.uac2 ? 98u : 3u), 0,
                                 &s_control);
    if (rc == ESP_OK) {
        s_control->callback = control_callback;
        rc = submit_control_step(s_format.uac2 ? 4u : 1u);
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
    s_clock_retry_step = 0;
    set_accepting(false);
    s_streaming = false;
    s_configuring = false;
    s_stopping = s_claimed || s_control || s_control_active;
    s_device_gone = s_device_gone || device_gone;
    lower_to_transition_priority();
}

bool controller_usb_audio_stream_poll_cleanup(void)
{
    if (s_clock_retry_step && s_configuring && !s_stopping && !s_control_active) {
        const TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - s_clock_change_tick) >= pdMS_TO_TICKS(500u)) {
            s_clock_retry_step = 0;
            if (!try_uac2_rate_fallback()) mark_fault(true);
        } else if ((TickType_t)(now - s_clock_retry_tick) >= pdMS_TO_TICKS(50u)) {
            const uint8_t step = s_clock_retry_step;
            s_clock_retry_step = 0;
            ++s_clock_readback_retries;
            if (submit_control_step(step) != ESP_OK) mark_fault(true);
        }
    }
    uint32_t feedback_timeout_ms = 500u;
    if (!s_format.uac2 && s_format.requires_feedback) {
        const uint32_t refresh_timeout = (1u << s_format.feedback_refresh) * 3u;
        if (refresh_timeout > feedback_timeout_ms) feedback_timeout_ms = refresh_timeout;
    }
    if (s_streaming && (s_format.uac2 || s_format.requires_feedback) &&
        (TickType_t)(xTaskGetTickCount() - s_feedback_tick) >= pdMS_TO_TICKS(feedback_timeout_ms)) {
        mark_fault(false); /* A stale clock estimate must not run indefinitely. */
    }
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
        if (s_feedback_active) {
            const esp_err_t feedback_halt = usb_host_endpoint_halt(s_device, s_format.feedback_endpoint);
            if (feedback_halt == ESP_OK || feedback_halt == ESP_ERR_INVALID_STATE)
                (void)usb_host_endpoint_flush(s_device, s_format.feedback_endpoint);
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
            if (s_format.uac2) detach_idle_isoc(s_isoc[i]);
            else if (!release_idle_isoc(i)) return false;
        }
    }
    if (s_control) {
        if (usb_host_transfer_free(s_control) != ESP_OK) {
            return false;
        }
        s_control = NULL;
    }
    if (s_feedback) {
        if (usb_host_transfer_free(s_feedback) != ESP_OK) return false;
        s_feedback = NULL;
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
        /* Keep the legacy PCM-paced FLX4 MAIN mirror level. Dedicated phones
         * PCM already has its PFL blend and level applied by the mixer; cutting
         * it here again loses 12 dB even at maximum headphone LEVEL. */
        const unsigned attenuation = s_qualified_flx4 && !controller_usb_audio_stream_consumer_paced() ? 2u : 0u;
        for (size_t i = 0u; i < chunk; ++i) {
            const int16_t ml = master_samples ? master_samples[i * 2u] >> attenuation : 0;
            const int16_t mr = master_samples ? master_samples[i * 2u + 1u] >> attenuation : 0;
            const int16_t hl = headphone_samples ? headphone_samples[i * 2u] : ml;
            const int16_t hr = headphone_samples ? headphone_samples[i * 2u + 1u] : mr;
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
    out_stats->configuration_fault = s_configuration_fault;
    out_stats->fault_control_step = s_fault_control_step;
    out_stats->last_control_status = s_last_control_status;
    out_stats->last_control_bytes = s_last_control_bytes;
    out_stats->last_control_word = s_last_control_word;
    out_stats->clock_readback_retries = s_clock_readback_retries;
    out_stats->clock_reported_rate = s_clock_reported_rate;
    out_stats->prime_fault_stage = s_prime_fault_stage;
    out_stats->prime_fault_index = s_prime_fault_index;
    out_stats->prime_fault_code = s_prime_fault_code;
    out_stats->sample_rate = __atomic_load_n(&s_target_rate, __ATOMIC_ACQUIRE);
    out_stats->bits_per_sample = __atomic_load_n(&s_sample_bits, __ATOMIC_ACQUIRE);
    out_stats->high_speed = s_high_speed;
    out_stats->subslot_bytes = s_format.bytes_per_sample;
    out_stats->feedback_updates = __atomic_load_n(&s_feedback_updates, __ATOMIC_RELAXED);
    out_stats->feedback_invalid = __atomic_load_n(&s_feedback_invalid, __ATOMIC_RELAXED);
    out_stats->consumer_paced = controller_usb_audio_stream_consumer_paced();
    out_stats->wire_format = __atomic_load_n(&s_wire_format, __ATOMIC_ACQUIRE);
}
