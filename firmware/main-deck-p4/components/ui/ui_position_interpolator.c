#include "ui_position_interpolator.h"

static uint32_t clamp_to_duration(uint32_t position_ms, uint32_t duration_ms)
{
    if (duration_ms > 0 && position_ms > duration_ms) {
        return duration_ms;
    }
    return position_ms;
}

static uint32_t abs_delta_u32(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

void ui_position_interpolator_init(ui_position_interpolator_t *interp)
{
    if (!interp) {
        return;
    }

    interp->initialized = false;
    interp->last_playing = false;
    interp->anchor_position_ms = 0;
    interp->anchor_time_us = 0;
}

uint32_t ui_position_interpolator_update(ui_position_interpolator_t *interp,
                                         uint32_t snapshot_position_ms,
                                         uint32_t duration_ms,
                                         bool playing,
                                         uint32_t speed_permille,
                                         uint64_t now_us)
{
    if (!interp) {
        return clamp_to_duration(snapshot_position_ms, duration_ms);
    }

    snapshot_position_ms = clamp_to_duration(snapshot_position_ms, duration_ms);
    if (speed_permille == 0) {
        speed_permille = 1000;
    }

    if (!interp->initialized || !playing || !interp->last_playing) {
        interp->initialized = true;
        interp->last_playing = playing;
        interp->anchor_position_ms = snapshot_position_ms;
        interp->anchor_time_us = now_us;
        return snapshot_position_ms;
    }

    uint64_t elapsed_us = now_us >= interp->anchor_time_us
                        ? now_us - interp->anchor_time_us
                        : 0;
    uint64_t advance_ms = (elapsed_us * speed_permille) / 1000000u;
    uint64_t predicted = (uint64_t)interp->anchor_position_ms + advance_ms;
    uint32_t predicted_ms = predicted > UINT32_MAX ? UINT32_MAX : (uint32_t)predicted;
    predicted_ms = clamp_to_duration(predicted_ms, duration_ms);

    if (abs_delta_u32(snapshot_position_ms, predicted_ms) >
        UI_POSITION_INTERPOLATOR_REBASE_THRESHOLD_MS) {
        interp->last_playing = playing;
        interp->anchor_position_ms = snapshot_position_ms;
        interp->anchor_time_us = now_us;
        return snapshot_position_ms;
    }

    interp->last_playing = playing;
    return predicted_ms;
}
