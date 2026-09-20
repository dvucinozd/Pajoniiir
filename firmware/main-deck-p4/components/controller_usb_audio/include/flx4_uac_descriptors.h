#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t interface_num;
    uint8_t alternate_setting;
    uint8_t endpoint_addr;
    uint16_t max_packet_size;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint8_t bytes_per_sample;
    uint32_t sample_rates[8];
    uint8_t sample_rate_count;
} flx4_uac_playback_format_t;

typedef struct {
    flx4_uac_playback_format_t formats[8];
    uint8_t format_count;
} flx4_uac_descriptor_result_t;

bool flx4_uac_parse_playback_formats(const uint8_t *config_desc,
                                     size_t config_len,
                                     flx4_uac_descriptor_result_t *out);

bool flx4_uac_select_preferred_format(const flx4_uac_descriptor_result_t *result,
                                      flx4_uac_playback_format_t *out);

#ifdef __cplusplus
}
#endif
