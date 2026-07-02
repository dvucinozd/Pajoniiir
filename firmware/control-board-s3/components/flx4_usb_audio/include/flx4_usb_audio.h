#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb/usb_host.h"
#include "flx4_uac_descriptors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool configured;
    flx4_uac_playback_format_t format;
    uint32_t submitted_packets;
    uint32_t completed_packets;
    uint32_t skipped_packets;
    uint32_t underrun_packets;
    uint32_t actual_bytes;
} flx4_usb_audio_stats_t;

esp_err_t flx4_usb_audio_configure(usb_host_client_handle_t client,
                                   usb_device_handle_t device,
                                   const uint8_t *config_desc,
                                   size_t config_len);
esp_err_t flx4_usb_audio_start_tone(uint16_t hz);
esp_err_t flx4_usb_audio_start_ring(void);

/*
 * Poll-driven ring autostart. Call periodically from the FLX4 USB client task
 * (the context that pumps usb_host_client_handle_events). Once the interface
 * is configured and the P4 audio link has buffered at least ~20 ms of frames,
 * it matches the FLX4 endpoint sample rate to the P4 output rate (if the FLX4
 * format supports it) and starts ring streaming exactly once. No-op otherwise.
 */
esp_err_t flx4_usb_audio_poll_ring_autostart(void);

void flx4_usb_audio_stop(void);
void flx4_usb_audio_get_stats(flx4_usb_audio_stats_t *out);

size_t flx4_usb_audio_fill_next_tone_packet(uint8_t *dst, size_t dst_capacity);

#ifdef __cplusplus
}
#endif
