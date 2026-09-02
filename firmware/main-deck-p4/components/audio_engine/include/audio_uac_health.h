#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AUDIO_UAC_HEALTH_NONE          = 0u,
    AUDIO_UAC_HEALTH_PRESSURE_LOW  = 1u << 0,
    AUDIO_UAC_HEALTH_PRESSURE_HIGH = 1u << 1,
    AUDIO_UAC_HEALTH_DROPPED       = 1u << 2,
    AUDIO_UAC_HEALTH_OVERFLOW      = 1u << 3,
    AUDIO_UAC_HEALTH_UNDERFLOW     = 1u << 4,
} audio_uac_health_flags_t;

typedef enum {
    AUDIO_UAC_RING_UNAVAILABLE = 0,
    AUDIO_UAC_RING_IDLE,
    AUDIO_UAC_RING_LOW,
    AUDIO_UAC_RING_NOMINAL,
    AUDIO_UAC_RING_HIGH,
} audio_uac_ring_state_t;

typedef struct {
    bool initialized;
    bool last_playback_active;
    uint32_t last_dropped_blocks;
    uint32_t last_overflow_frames;
    uint32_t last_underflow_frames;
    uint32_t active_data_loss_flags;
} audio_uac_health_monitor_t;

typedef struct {
    uint32_t flags;
    uint32_t low_alarm_frames;
    uint32_t high_alarm_frames;
    uint32_t delta_dropped_blocks;
    uint32_t delta_overflow_frames;
    uint32_t delta_underflow_frames;
    uint32_t active_data_loss_flags;
} audio_uac_health_result_t;

uint32_t audio_uac_ring_low_alarm_frames(uint32_t capacity_frames);
uint32_t audio_uac_ring_high_alarm_frames(uint32_t capacity_frames);
audio_uac_ring_state_t audio_uac_ring_state(bool playback_active,
                                            uint32_t submitted_blocks,
                                            uint32_t queued_frames,
                                            uint32_t capacity_frames);
const char *audio_uac_ring_state_name(audio_uac_ring_state_t state);
void audio_uac_health_reset(audio_uac_health_monitor_t *monitor);
audio_uac_health_result_t audio_uac_health_sample(
    audio_uac_health_monitor_t *monitor,
    bool playback_active,
    uint32_t submitted_blocks,
    uint32_t queued_frames,
    uint32_t capacity_frames,
    uint32_t dropped_blocks,
    uint32_t overflow_frames,
    uint32_t underflow_frames);
