#include "audio_uac_health.h"

#include <stddef.h>
#include <string.h>

uint32_t audio_uac_ring_low_alarm_frames(uint32_t capacity_frames)
{
    return capacity_frames / 4u;
}

uint32_t audio_uac_ring_high_alarm_frames(uint32_t capacity_frames)
{
    return (uint32_t)(((uint64_t)capacity_frames * 3u) / 4u);
}

audio_uac_ring_state_t audio_uac_ring_state(bool playback_active,
                                            uint32_t submitted_blocks,
                                            uint32_t queued_frames,
                                            uint32_t capacity_frames)
{
    if (capacity_frames == 0u || submitted_blocks == 0u) {
        return AUDIO_UAC_RING_UNAVAILABLE;
    }
    if (!playback_active) return AUDIO_UAC_RING_IDLE;
    if (queued_frames < audio_uac_ring_low_alarm_frames(capacity_frames)) {
        return AUDIO_UAC_RING_LOW;
    }
    if (queued_frames > audio_uac_ring_high_alarm_frames(capacity_frames)) {
        return AUDIO_UAC_RING_HIGH;
    }
    return AUDIO_UAC_RING_NOMINAL;
}

const char *audio_uac_ring_state_name(audio_uac_ring_state_t state)
{
    switch (state) {
    case AUDIO_UAC_RING_IDLE:    return "idle";
    case AUDIO_UAC_RING_LOW:     return "low";
    case AUDIO_UAC_RING_NOMINAL: return "nominal";
    case AUDIO_UAC_RING_HIGH:    return "high";
    case AUDIO_UAC_RING_UNAVAILABLE:
    default:                     return "unavailable";
    }
}

void audio_uac_health_reset(audio_uac_health_monitor_t *monitor)
{
    if (monitor) memset(monitor, 0, sizeof(*monitor));
}

static uint32_t counter_delta(uint32_t current, uint32_t previous)
{
    return current >= previous ? current - previous : 0u;
}

audio_uac_health_result_t audio_uac_health_sample(
    audio_uac_health_monitor_t *monitor,
    bool playback_active,
    uint32_t submitted_blocks,
    uint32_t queued_frames,
    uint32_t capacity_frames,
    uint32_t dropped_blocks,
    uint32_t overflow_frames,
    uint32_t underflow_frames)
{
    audio_uac_health_result_t result = {
        .low_alarm_frames = audio_uac_ring_low_alarm_frames(capacity_frames),
        .high_alarm_frames = audio_uac_ring_high_alarm_frames(capacity_frames),
    };
    if (!monitor) return result;

    const bool playback_started = playback_active &&
                                  (!monitor->initialized ||
                                   !monitor->last_playback_active);
    if (monitor->initialized) {
        result.delta_dropped_blocks =
            counter_delta(dropped_blocks, monitor->last_dropped_blocks);
        result.delta_overflow_frames =
            counter_delta(overflow_frames, monitor->last_overflow_frames);
        result.delta_underflow_frames =
            counter_delta(underflow_frames, monitor->last_underflow_frames);
    }

    monitor->last_dropped_blocks = dropped_blocks;
    monitor->last_overflow_frames = overflow_frames;
    monitor->last_underflow_frames = underflow_frames;
    monitor->last_playback_active = playback_active;
    monitor->initialized = true;

    if (!playback_active) {
        result.delta_dropped_blocks = 0u;
        result.delta_overflow_frames = 0u;
        result.delta_underflow_frames = 0u;
        return result;
    }
    if (playback_started) {
        result.delta_dropped_blocks = 0u;
        result.delta_overflow_frames = 0u;
        result.delta_underflow_frames = 0u;
    }

    audio_uac_ring_state_t state = audio_uac_ring_state(
        playback_active, submitted_blocks, queued_frames, capacity_frames);
    if (state == AUDIO_UAC_RING_LOW) result.flags |= AUDIO_UAC_HEALTH_PRESSURE_LOW;
    else if (state == AUDIO_UAC_RING_HIGH) result.flags |= AUDIO_UAC_HEALTH_PRESSURE_HIGH;
    if (result.delta_dropped_blocks > 0u) result.flags |= AUDIO_UAC_HEALTH_DROPPED;
    if (result.delta_overflow_frames > 0u) result.flags |= AUDIO_UAC_HEALTH_OVERFLOW;
    if (result.delta_underflow_frames > 0u) result.flags |= AUDIO_UAC_HEALTH_UNDERFLOW;
    return result;
}
