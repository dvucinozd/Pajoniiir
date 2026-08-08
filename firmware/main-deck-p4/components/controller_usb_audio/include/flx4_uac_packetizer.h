#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bytes_per_sample;
    uint32_t frame_accum;
} flx4_uac_packetizer_t;

void flx4_uac_packetizer_init(flx4_uac_packetizer_t *p,
                              uint32_t sample_rate,
                              uint8_t channels,
                              uint8_t bytes_per_sample);

uint16_t flx4_uac_packetizer_next_frames(flx4_uac_packetizer_t *p);
size_t flx4_uac_packetizer_next_bytes(flx4_uac_packetizer_t *p);

#ifdef __cplusplus
}
#endif
