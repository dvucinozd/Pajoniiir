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
    bool claimed;
    bool configuring;
    bool streaming;
    bool faulted;
} controller_usb_audio_stream_stats_t;

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
