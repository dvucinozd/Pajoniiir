/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v263: exact PVBR seeks. The Rekordbox PVBR table holds AUDIO_PVBR_LEN byte
 * offsets, entry i at i/len of the track, so a seek restarts decoding at the
 * entry at or before the target (duration/400 apart, ~0.4 s on a 3 min
 * track). The deck and the waveform report the target from the first frame
 * on; v262 hardware: after a hot cue or a touch seek the audio trailed the
 * waveform by 100-300 ms. The decoder now drops the frames between the entry
 * and the target before publishing any.
 */

/* Table entry a seek to position_ms uses (clamped to len - 1). */
uint32_t audio_pvbr_index(uint32_t position_ms, uint32_t duration_ms, uint32_t len);
/* Track time of entry idx, never past position_ms. */
uint32_t audio_pvbr_entry_ms(uint32_t idx, uint32_t duration_ms, uint32_t len,
                             uint32_t position_ms);
/* Decoded frames between the entry and the target. */
uint32_t audio_seek_skip_frames(uint32_t target_ms, uint32_t entry_ms, uint32_t sample_rate);
/* Takes up to `samples` frames off *skip and returns how many of this batch
 * to drop from its start. */
uint32_t audio_seek_skip_take(uint32_t *skip, uint32_t samples);

#ifdef __cplusplus
}
#endif
