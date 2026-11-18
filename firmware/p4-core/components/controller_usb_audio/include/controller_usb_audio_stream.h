#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t submitted_blocks;
    uint64_t dropped_blocks;
    uint64_t submitted_frames;
    uint32_t ring_queued_frames;
    uint32_t ring_capacity_frames;
    uint32_t ring_high_water_frames;
    uint64_t overrun_frames;
    uint64_t underrun_frames;
    uint64_t clock_trimmed_frames;
    uint64_t clock_duplicated_frames;
    uint32_t config_failures;
    uint32_t transfer_failures;
    uint32_t packet_failures;
    uint64_t packet_lost_frames;
    uint32_t stream_epoch;
    bool claimed;
    bool configuring;
    bool streaming;
    bool faulted;
    bool configuration_fault;
    uint8_t fault_control_step;
    int32_t last_control_status;
    int32_t last_control_bytes;
    uint32_t last_control_word;
    uint32_t clock_readback_retries;
    uint32_t clock_reported_rate;
    /* Startup: 1 feedback alloc, 2 feedback submit, 3 ISO alloc, 4 ISO submit,
     * 5 interface claim (index is the interface number). */
    uint8_t prime_fault_stage;
    uint8_t prime_fault_index;
    int32_t prime_fault_code;
    uint32_t sample_rate;
    uint8_t bits_per_sample;
    bool consumer_paced;
    bool high_speed;
    uint8_t subslot_bytes;
    uint32_t feedback_updates;
    uint32_t feedback_invalid;
    uint8_t wire_format; /* controller_uac_wire_format_t */
} controller_usb_audio_stream_stats_t;

/* How PCM is laid out on the isochronous OUT endpoint.
 * DESCRIPTOR follows the selected alternate setting's subslot size.
 * S16LE sends 16-bit little-endian samples (8 bytes per 4-channel frame)
 * and sizes packets for 16-bit even when a UAC1 alternate advertises 3-byte
 * subslots. The Pioneer DDJ-SB3 advertises 24-bit but consumes 16-bit LE;
 * 24-bit or zero-padded packets play distorted and at the wrong pitch.
 * S16LE has no effect on UAC2 or on alternates that are already 16-bit. */
typedef enum {
    CONTROLLER_UAC_WIRE_DESCRIPTOR = 0,
    CONTROLLER_UAC_WIRE_S16LE = 1,
} controller_uac_wire_format_t;

/* Any task, any time; takes effect on the next submitted transfer. The value
 * persists across stream restarts, so the profile owner must reset it to
 * DESCRIPTOR when the controller disconnects. */
esp_err_t controller_usb_audio_stream_set_wire_format(controller_uac_wire_format_t format);

/* Owner task only; only while quiesced. FLX4 preserves its qualified format/gain. */
esp_err_t controller_usb_audio_stream_set_policy(bool qualified_flx4, bool consumer_paced,
                                                 bool high_speed);
esp_err_t controller_usb_audio_stream_set_pacing(bool consumer_paced);
bool controller_usb_audio_stream_consumer_paced(void);
bool controller_usb_audio_stream_available(void);
bool controller_usb_audio_stream_needs_recovery(void);
/* HS configuration rejection may stop audio while preserving MIDI ownership. */
bool controller_usb_audio_stream_fault_isolatable(void);
/* Producer waits for actual ring free space, bounded and cancellation-aware. */
esp_err_t controller_usb_audio_stream_wait_room(uint32_t source_frames,
                                               uint32_t source_rate, uint32_t timeout_ms);

esp_err_t controller_usb_audio_stream_start(
    usb_host_client_handle_t client,
    usb_device_handle_t device,
    const uint8_t *config_descriptor,
    size_t config_descriptor_length,
    TaskHandle_t owner_task,
    UBaseType_t active_priority,
    UBaseType_t transition_priority);
void controller_usb_audio_stream_request_stop(bool device_gone);
bool controller_usb_audio_stream_poll_cleanup(void);
bool controller_usb_audio_stream_is_quiesced(void);

esp_err_t controller_usb_audio_stream_write(const int16_t *master_samples,
                                            const int16_t *headphone_samples,
                                            size_t frame_count,
                                            uint32_t source_sample_rate);
void controller_usb_audio_stream_get_stats(
    controller_usb_audio_stream_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

/* SPDX-License-Identifier: Apache-2.0 */
