#include "audio_uac_health.h"

#include <assert.h>
#include <stdio.h>

static audio_uac_health_result_t sample(audio_uac_health_monitor_t *monitor,
                                        bool active,
                                        uint32_t submitted,
                                        uint32_t queued,
                                        uint32_t dropped,
                                        uint32_t overflow,
                                        uint32_t underflow)
{
    return audio_uac_health_sample(monitor, active, submitted, queued, 2048u,
                                   dropped, overflow, underflow);
}

static void test_ring_thresholds_and_states(void)
{
    assert(audio_uac_ring_low_alarm_frames(2048u) == 512u);
    assert(audio_uac_ring_high_alarm_frames(2048u) == 1536u);
    assert(audio_uac_ring_state(false, 10u, 0u, 2048u) == AUDIO_UAC_RING_IDLE);
    assert(audio_uac_ring_state(true, 0u, 0u, 2048u) == AUDIO_UAC_RING_UNAVAILABLE);
    assert(audio_uac_ring_state(true, 10u, 511u, 2048u) == AUDIO_UAC_RING_LOW);
    assert(audio_uac_ring_state(true, 10u, 512u, 2048u) == AUDIO_UAC_RING_NOMINAL);
    assert(audio_uac_ring_state(true, 10u, 1536u, 2048u) == AUDIO_UAC_RING_NOMINAL);
    assert(audio_uac_ring_state(true, 10u, 1537u, 2048u) == AUDIO_UAC_RING_HIGH);
    assert(audio_uac_ring_state(true, 10u, 0u, 0u) == AUDIO_UAC_RING_UNAVAILABLE);
}

static void test_pressure_and_active_data_loss(void)
{
    audio_uac_health_monitor_t monitor = {0};
    audio_uac_health_result_t r = sample(&monitor, true, 1u, 1024u, 8u, 9u, 10u);
    assert(r.flags == AUDIO_UAC_HEALTH_NONE);
    r = sample(&monitor, true, 2u, 511u, 8u, 9u, 10u);
    assert(r.flags == AUDIO_UAC_HEALTH_PRESSURE_LOW);
    r = sample(&monitor, true, 3u, 1537u, 10u, 12u, 14u);
    assert(r.flags == (AUDIO_UAC_HEALTH_PRESSURE_HIGH |
                       AUDIO_UAC_HEALTH_DROPPED |
                       AUDIO_UAC_HEALTH_OVERFLOW |
                       AUDIO_UAC_HEALTH_UNDERFLOW));
    assert(r.delta_dropped_blocks == 2u);
    assert(r.delta_overflow_frames == 3u);
    assert(r.delta_underflow_frames == 4u);
}

static void test_idle_and_playback_start_establish_baseline(void)
{
    audio_uac_health_monitor_t monitor = {0};
    (void)sample(&monitor, false, 10u, 0u, 0u, 0u, 100000u);
    audio_uac_health_result_t r = sample(&monitor, false, 10u, 0u,
                                         1u, 2u, 200000u);
    assert(r.flags == AUDIO_UAC_HEALTH_NONE);
    r = sample(&monitor, true, 11u, 1024u, 4u, 6u, 213582u);
    assert(r.flags == AUDIO_UAC_HEALTH_NONE);
    assert(r.delta_dropped_blocks == 0u);
    assert(r.delta_overflow_frames == 0u);
    assert(r.delta_underflow_frames == 0u);
    r = sample(&monitor, true, 12u, 1024u, 5u, 8u, 213585u);
    assert(r.flags == (AUDIO_UAC_HEALTH_DROPPED |
                       AUDIO_UAC_HEALTH_OVERFLOW |
                       AUDIO_UAC_HEALTH_UNDERFLOW));
}

static void test_counter_reset_does_not_wrap(void)
{
    audio_uac_health_monitor_t monitor = {0};
    (void)sample(&monitor, true, 20u, 1024u, 100u, 200u, 300u);
    audio_uac_health_result_t r = sample(&monitor, true, 1u, 1024u, 0u, 0u, 0u);
    assert(r.flags == AUDIO_UAC_HEALTH_NONE);
}

int main(void)
{
    test_ring_thresholds_and_states();
    test_pressure_and_active_data_loss();
    test_idle_and_playback_start_establish_baseline();
    test_counter_reset_does_not_wrap();
    puts("audio_uac_health tests passed");
    return 0;
}
