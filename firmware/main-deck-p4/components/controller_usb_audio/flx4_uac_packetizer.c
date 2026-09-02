#include "flx4_uac_packetizer.h"

void flx4_uac_packetizer_init(flx4_uac_packetizer_t *p,
                              uint32_t sample_rate,
                              uint8_t channels,
                              uint8_t bytes_per_sample)
{
    if (!p) {
        return;
    }

    p->sample_rate = sample_rate;
    p->channels = channels;
    p->bytes_per_sample = bytes_per_sample;
    p->frame_accum = 0u;
}

uint16_t flx4_uac_packetizer_next_frames(flx4_uac_packetizer_t *p)
{
    if (!p || p->sample_rate == 0u) {
        return 0u;
    }

    p->frame_accum += p->sample_rate;
    const uint16_t frames = (uint16_t)(p->frame_accum / 1000u);
    p->frame_accum -= (uint32_t)frames * 1000u;
    return frames;
}

size_t flx4_uac_packetizer_next_bytes(flx4_uac_packetizer_t *p)
{
    const uint16_t frames = flx4_uac_packetizer_next_frames(p);
    if (!p) {
        return 0u;
    }

    return (size_t)frames * (size_t)p->channels * (size_t)p->bytes_per_sample;
}
