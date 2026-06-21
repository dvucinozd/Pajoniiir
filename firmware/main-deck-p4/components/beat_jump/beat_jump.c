#include "beat_jump.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

uint32_t beat_jump_calculate_target_ms(uint32_t position_ms,
                                       uint16_t bpm,
                                       int beat_shift,
                                       const anlz_metadata_t *meta)
{
    if (meta && meta->beats && meta->beat_count > 0) {
        uint16_t closest_idx = 0;
        uint32_t min_diff = UINT32_MAX;
        for (uint16_t i = 0; i < meta->beat_count; i++) {
            uint32_t beat_ms = meta->beats[i].time_ms;
            uint32_t diff = position_ms > beat_ms ? position_ms - beat_ms : beat_ms - position_ms;
            if (diff < min_diff) {
                min_diff = diff;
                closest_idx = i;
            }
        }

        int target_idx = (int)closest_idx + beat_shift;
        if (target_idx < 0) {
            target_idx = 0;
        }
        if (target_idx >= (int)meta->beat_count) {
            target_idx = (int)meta->beat_count - 1;
        }
        return meta->beats[target_idx].time_ms;
    }

    uint16_t safe_bpm = bpm > 0 ? bpm : 120u;
    int64_t beat_len_ms = 60000 / safe_bpm;
    int64_t target_ms = (int64_t)position_ms + (beat_len_ms * (int64_t)beat_shift);
    return target_ms > 0 ? (uint32_t)target_ms : 0u;
}
