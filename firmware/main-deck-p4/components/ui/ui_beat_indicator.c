#include "ui_beat_indicator.h"
#include <limits.h>

#ifndef UINT32_MAX
#define UINT32_MAX (0xFFFFFFFFu)
#endif

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

static uint16_t clamp_state_progress(uint16_t progress_permille)
{
    return progress_permille > 1000u ? 1000u : progress_permille;
}

static int16_t state_to_bar_permille(ui_beat_indicator_state_t state)
{
    return (int16_t)(((uint16_t)(state.phase % 4u) * 1000u) +
                     clamp_state_progress(state.progress_permille));
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

ui_beat_phase_delta_t ui_beat_phase_delta_calculate(ui_beat_indicator_state_t deck1,
                                                    ui_beat_indicator_state_t deck2)
{
    if (!deck1.valid || !deck2.valid) {
        return (ui_beat_phase_delta_t){0};
    }

    int16_t delta = (int16_t)(state_to_bar_permille(deck2) -
                              state_to_bar_permille(deck1));
    while (delta > 2000) {
        delta = (int16_t)(delta - 4000);
    }
    while (delta < -2000) {
        delta = (int16_t)(delta + 4000);
    }

    int16_t abs_delta = delta < 0 ? (int16_t)-delta : delta;
    return (ui_beat_phase_delta_t){
        .valid = true,
        .deck2_delta_permille = delta,
        .locked = abs_delta <= 50,
    };
}

typedef struct {
    const anlz_beat_t *beats;
    uint16_t last_idx;
} beat_cache_entry_t;

static beat_cache_entry_t s_beat_cache[2] = {
    { .beats = NULL, .last_idx = 0 },
    { .beats = NULL, .last_idx = 0 }
};

static uint16_t find_beat_index(uint32_t position_ms, const anlz_beat_t *beats, uint16_t beat_count)
{
    if (beat_count == 0) return 0;

    int slot = -1;
    if (s_beat_cache[0].beats == beats) {
        slot = 0;
    } else if (s_beat_cache[1].beats == beats) {
        slot = 1;
    } else {
        static int next_slot = 0;
        slot = next_slot;
        s_beat_cache[slot].beats = beats;
        s_beat_cache[slot].last_idx = 0;
        next_slot = (next_slot + 1) % 2;
    }

    uint16_t last_idx = s_beat_cache[slot].last_idx;
    if (last_idx >= beat_count) {
        last_idx = 0;
    }

    uint32_t t_current = beats[last_idx].time_ms;
    uint32_t t_next = (last_idx + 1 < beat_count) ? beats[last_idx + 1].time_ms : UINT32_MAX;

    if (position_ms >= t_current && position_ms < t_next) {
        return last_idx;
    }

    if (last_idx + 1 < beat_count) {
        uint32_t t_next_next = (last_idx + 2 < beat_count) ? beats[last_idx + 2].time_ms : UINT32_MAX;
        if (position_ms >= t_next && position_ms < t_next_next) {
            s_beat_cache[slot].last_idx = last_idx + 1;
            return last_idx + 1;
        }
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

    s_beat_cache[slot].last_idx = idx;
    return idx;
}

ui_beat_indicator_state_t ui_beat_indicator_calculate(uint32_t position_ms,
                                                       const anlz_beat_t *beats,
                                                       uint16_t beat_count,
                                                       uint16_t fallback_bpm)
{
    if (!beats || beat_count == 0) {
        return from_bpm(position_ms, fallback_bpm);
    }

    uint16_t idx = find_beat_index(position_ms, beats, beat_count);

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
