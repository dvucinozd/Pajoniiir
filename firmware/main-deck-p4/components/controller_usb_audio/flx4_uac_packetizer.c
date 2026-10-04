#include "flx4_uac_packetizer.h"

bool controller_uac_pack_pcm(uint8_t *dst, size_t bytes, const int16_t *src,
                             size_t frames, uint8_t sample_bytes)
{
    if (!dst || !src || (sample_bytes != 2u && sample_bytes != 3u) ||
        frames > SIZE_MAX / (4u * sample_bytes) || bytes < frames * 4u * sample_bytes)
        return false;
    for (size_t i = 0; i < frames * 4u; ++i) {
        uint32_t sample = (uint32_t)((int32_t)src[i] * (sample_bytes == 3u ? 256 : 1));
        for (uint8_t b = 0; b < sample_bytes; ++b) {
            *dst++ = (uint8_t)(sample >> (8u * b));
        }
    }
    return true;
}

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
