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
    uint32_t playback_session_epoch,
    uint32_t uac_stream_epoch,
    uint32_t submitted_blocks,
    uint32_t queued_frames,
    uint32_t capacity_frames,
    uint32_t dropped_blocks,
    uint32_t overflow_frames,
    uint32_t underflow_frames,
    uint32_t packet_lost_frames)
{
    audio_uac_health_result_t result = {
        .low_alarm_frames = audio_uac_ring_low_alarm_frames(capacity_frames),
        .high_alarm_frames = audio_uac_ring_high_alarm_frames(capacity_frames),
    };
    if (!monitor) return result;

    /* A STOP -> PLAY interval can fit entirely between two health callbacks.
     * The sampled active boolean then remains true and cannot distinguish the
     * new playback session from the old one. The audio engine owns the exact
     * transition and advances this epoch whenever all-idle becomes active. */
    const bool playback_started = playback_active &&
                                  (!monitor->initialized ||
                                   !monitor->last_playback_active ||
                                   playback_session_epoch !=
                                       monitor->last_playback_session_epoch);
    /* The controller can disappear and return while both decks keep playing.
     * Its freshly primed isochronous consumer may observe an empty ring before
     * the producer's first block. A stream epoch distinguishes that boundary
     * from a genuine underflow later in the same UAC connection. Epoch zero is
     * deliberately ignored so disconnect itself cannot clear a latched fault. */
    const bool stream_started = playback_active && monitor->initialized &&
                                uac_stream_epoch != 0u &&
                                uac_stream_epoch != monitor->last_uac_stream_epoch;
    if (monitor->initialized) {
        result.delta_dropped_blocks =
            counter_delta(dropped_blocks, monitor->last_dropped_blocks);
        result.delta_overflow_frames =
            counter_delta(overflow_frames, monitor->last_overflow_frames);
        result.delta_underflow_frames =
            counter_delta(underflow_frames, monitor->last_underflow_frames);
        result.delta_packet_lost_frames =
            counter_delta(packet_lost_frames, monitor->last_packet_lost_frames);
    }

    monitor->last_dropped_blocks = dropped_blocks;
    monitor->last_overflow_frames = overflow_frames;
    monitor->last_underflow_frames = underflow_frames;
    monitor->last_packet_lost_frames = packet_lost_frames;
    monitor->last_playback_active = playback_active;
    monitor->last_playback_session_epoch = playback_session_epoch;
    monitor->last_uac_stream_epoch = uac_stream_epoch;
    monitor->initialized = true;

    if (!playback_active) {
        monitor->active_data_loss_flags = AUDIO_UAC_HEALTH_NONE;
        monitor->startup_underflow_grace_pending = false;
        result.delta_dropped_blocks = 0u;
        result.delta_overflow_frames = 0u;
        result.delta_underflow_frames = 0u;
        result.delta_packet_lost_frames = 0u;
        return result;
    }
    if (playback_started) {
        monitor->active_data_loss_flags = AUDIO_UAC_HEALTH_NONE;
        monitor->startup_underflow_grace_pending = true;
        result.delta_dropped_blocks = 0u;
        result.delta_overflow_frames = 0u;
        result.delta_underflow_frames = 0u;
        result.delta_packet_lost_frames = 0u;
    } else if (stream_started) {
        /* A new physical UAC stream starts a new health session, but only its
         * producer-prime underflow is eligible for grace. Transport loss,
         * producer drops and ring overflow on reconnect remain reportable. */
        monitor->active_data_loss_flags = AUDIO_UAC_HEALTH_NONE;
        monitor->startup_underflow_grace_pending = true;
        result.delta_underflow_frames = 0u;
    }

    audio_uac_ring_state_t state = audio_uac_ring_state(
        playback_active, submitted_blocks, queued_frames, capacity_frames);
    /* The UAC isochronous consumer runs continuously and zero-fills an empty
     * ring while playback is idle. The first producer transition can therefore
     * leave an expected empty-read delta between the sample that observes PLAY
     * and the sample that observes the primed ring. Ignore that delta exactly
     * once, and only when the follow-up sample proves the ring recovered. A
     * ring that remains low/unavailable still reports the underflow, as does
     * every post-prime interval. */
    if (!playback_started && !stream_started &&
        monitor->startup_underflow_grace_pending) {
        if (state == AUDIO_UAC_RING_NOMINAL ||
            state == AUDIO_UAC_RING_HIGH) {
            result.delta_underflow_frames = 0u;
        }
        monitor->startup_underflow_grace_pending = false;
    }
    if (state == AUDIO_UAC_RING_LOW) result.flags |= AUDIO_UAC_HEALTH_PRESSURE_LOW;
    else if (state == AUDIO_UAC_RING_HIGH) result.flags |= AUDIO_UAC_HEALTH_PRESSURE_HIGH;
    if (result.delta_dropped_blocks > 0u) result.flags |= AUDIO_UAC_HEALTH_DROPPED;
    if (result.delta_overflow_frames > 0u) result.flags |= AUDIO_UAC_HEALTH_OVERFLOW;
    if (result.delta_underflow_frames > 0u) result.flags |= AUDIO_UAC_HEALTH_UNDERFLOW;
    if (result.delta_packet_lost_frames > 0u) result.flags |= AUDIO_UAC_HEALTH_PACKET_LOSS;
    monitor->active_data_loss_flags |= result.flags &
        (AUDIO_UAC_HEALTH_DROPPED |
         AUDIO_UAC_HEALTH_OVERFLOW |
         AUDIO_UAC_HEALTH_UNDERFLOW | AUDIO_UAC_HEALTH_PACKET_LOSS);
    result.active_data_loss_flags = monitor->active_data_loss_flags;
    return result;
}
