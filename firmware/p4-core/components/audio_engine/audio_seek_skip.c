/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#include "audio_seek_skip.h"

uint32_t audio_pvbr_index(uint32_t position_ms, uint32_t duration_ms, uint32_t len)
{
    if (len == 0u) return 0u;
    if (duration_ms > 0u && position_ms > duration_ms) position_ms = duration_ms;
    uint32_t idx = duration_ms > 0u
        ? (uint32_t)(((uint64_t)position_ms * len) / duration_ms)
        : 0u;
    return idx >= len ? len - 1u : idx;
}

uint32_t audio_pvbr_entry_ms(uint32_t idx, uint32_t duration_ms, uint32_t len,
                             uint32_t position_ms)
{
    if (len == 0u) return position_ms;
    uint32_t entry_ms = (uint32_t)(((uint64_t)idx * duration_ms) / len);
    return entry_ms > position_ms ? position_ms : entry_ms;
}

uint32_t audio_seek_skip_frames(uint32_t target_ms, uint32_t entry_ms, uint32_t sample_rate)
{
    if (sample_rate == 0u || target_ms <= entry_ms) return 0u;
    return (uint32_t)(((uint64_t)(target_ms - entry_ms) * sample_rate) / 1000u);
}

uint32_t audio_seek_skip_take(uint32_t *skip, uint32_t samples)
{
    if (!skip) return 0u;
    uint32_t drop = *skip < samples ? *skip : samples;
    *skip -= drop;
    return drop;
}
