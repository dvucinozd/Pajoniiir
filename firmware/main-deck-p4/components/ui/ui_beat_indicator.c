#include "ui_beat_indicator.h"

static ui_beat_indicator_state_t invalid_state(void)
{
    return (ui_beat_indicator_state_t){0};
}

static uint16_t clamp_progress(uint32_t elapsed_ms, uint32_t beat_len_ms)
{
    if (beat_len_ms == 0) {
        return 0;
    }
    if (elapsed_ms >= beat_len_ms) {
        return 1000;
    }
    return (uint16_t)((elapsed_ms * 1000u) / beat_len_ms);
}

static ui_beat_indicator_state_t from_bpm(uint32_t position_ms, uint16_t bpm)
{
    if (bpm == 0) {
        return invalid_state();
    }

    uint32_t beat_len_ms = 60000u / bpm;
    if (beat_len_ms == 0) {
        return invalid_state();
    }

    uint32_t beat_idx = position_ms / beat_len_ms;
    uint32_t elapsed = position_ms % beat_len_ms;
    uint8_t phase = (uint8_t)(beat_idx % 4u);

    return (ui_beat_indicator_state_t){
        .valid = true,
        .phase = phase,
        .downbeat = (phase == 0),
        .progress_permille = clamp_progress(elapsed, beat_len_ms),
    };
}

ui_beat_indicator_state_t ui_beat_indicator_calculate(uint32_t position_ms,
                                                       const anlz_beat_t *beats,
                                                       uint16_t beat_count,
                                                       uint16_t fallback_bpm)
{
    if (!beats || beat_count == 0) {
        return from_bpm(position_ms, fallback_bpm);
    }

    uint16_t idx = 0;
    if (position_ms <= beats[0].time_ms) {
        idx = 0;
    } else {
        uint16_t low = 0;
        uint16_t high = (uint16_t)(beat_count - 1);
        while (low < high) {
            uint16_t mid = (uint16_t)(low + ((high - low + 1u) / 2u));
            if (beats[mid].time_ms <= position_ms) {
                low = mid;
            } else {
                high = (uint16_t)(mid - 1u);
            }
        }
        idx = low;
    }

    uint32_t beat_start = beats[idx].time_ms;
    uint32_t beat_len_ms = 0;
    if ((uint16_t)(idx + 1u) < beat_count && beats[idx + 1u].time_ms > beat_start) {
        beat_len_ms = beats[idx + 1u].time_ms - beat_start;
    } else if (beats[idx].bpm_x100 > 0) {
        beat_len_ms = 6000000u / beats[idx].bpm_x100;
    } else if (fallback_bpm > 0) {
        beat_len_ms = 60000u / fallback_bpm;
    }

    uint32_t elapsed = position_ms > beat_start ? position_ms - beat_start : 0;
    uint8_t phase = (uint8_t)(beats[idx].beat_phase % 4u);

    return (ui_beat_indicator_state_t){
        .valid = true,
        .phase = phase,
        .downbeat = (phase == 0),
        .progress_permille = clamp_progress(elapsed, beat_len_ms),
    };
}
