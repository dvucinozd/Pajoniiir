#include "flx4_usb_audio.h"

#include <string.h>

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
#define FLX4_USB_AUDIO_TRANSFER_COUNT 4u
#define FLX4_USB_AUDIO_PACKETS_PER_TRANSFER 8u
static usb_transfer_t *s_transfers[FLX4_USB_AUDIO_TRANSFER_COUNT];
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
static void free_transfers(void)
{
    for (uint8_t i = 0; i < FLX4_USB_AUDIO_TRANSFER_COUNT; ++i) {
        if (s_transfers[i]) {
            (void)usb_host_transfer_free(s_transfers[i]);
            s_transfers[i] = NULL;
        }
    }
}

static esp_err_t allocate_transfers(const flx4_uac_playback_format_t *fmt)
{
    if (!fmt || fmt->max_packet_size == 0u) {
        return ESP_ERR_INVALID_ARG;
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
    esp_err_t claim_rc = usb_host_interface_claim(client,
                                                  device,
                                                  selected.interface_num,
                                                  selected.alternate_setting);
    if (claim_rc != ESP_OK) {
        flx4_usb_audio_stop();
        return claim_rc;
    }
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
    return ESP_OK;
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
    free_transfers();
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
