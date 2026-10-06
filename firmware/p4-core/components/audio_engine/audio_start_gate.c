#include "audio_start_gate.h"

bool audio_start_gate_ready(uint32_t future_frames,
                            uint32_t minimum_frames,
                            bool source_eof)
{
    if (minimum_frames == 0u) return true;
    if (future_frames >= minimum_frames) return true;
    /* A valid short tail can never reach the normal threshold. */
    return source_eof && future_frames > 0u;
}
