#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Pack four signed 16-bit engine channels to 16-bit or packed 24-bit PCM.
 * 24-bit output multiplies by 256 (full amplitude), without signed-shift UB. */

#ifdef __cplusplus
extern "C" {
#endif

bool controller_uac_pack_pcm(uint8_t *dst, size_t bytes, const int16_t *src,
                             size_t frames, uint8_t sample_bytes);

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
