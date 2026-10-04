#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Metadata remains the fallback and the immutable waveform time base. */
static inline uint32_t ui_track_duration_select(uint32_t metadata_ms,
                                                uint32_t expected_session,
                                                bool audio_loaded,
                                                uint32_t audio_session,
                                                uint32_t audio_ms)
{
    return expected_session != 0u && audio_loaded &&
           audio_session == expected_session && audio_ms != 0u
               ? audio_ms : metadata_ms;
}
