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
                                   dropped, overflow, underflow, 0u);
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
    assert(r.active_data_loss_flags == (AUDIO_UAC_HEALTH_DROPPED |
                                        AUDIO_UAC_HEALTH_OVERFLOW |
                                        AUDIO_UAC_HEALTH_UNDERFLOW));
    r = sample(&monitor, true, 4u, 1024u, 10u, 12u, 14u);
    assert(r.flags == AUDIO_UAC_HEALTH_NONE);
    assert(r.active_data_loss_flags == (AUDIO_UAC_HEALTH_DROPPED |
                                        AUDIO_UAC_HEALTH_OVERFLOW |
                                        AUDIO_UAC_HEALTH_UNDERFLOW));
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
    assert(r.active_data_loss_flags == AUDIO_UAC_HEALTH_NONE);
    r = sample(&monitor, true, 12u, 1024u, 5u, 8u, 213585u);
    assert(r.flags == (AUDIO_UAC_HEALTH_DROPPED |
                       AUDIO_UAC_HEALTH_OVERFLOW));
    assert(r.active_data_loss_flags == r.flags);
    assert(r.delta_underflow_frames == 0u);
    r = sample(&monitor, true, 13u, 1024u, 5u, 8u, 213588u);
    assert(r.flags == AUDIO_UAC_HEALTH_UNDERFLOW);
    assert(r.delta_underflow_frames == 3u);
    assert(r.active_data_loss_flags == (AUDIO_UAC_HEALTH_DROPPED |
                                        AUDIO_UAC_HEALTH_OVERFLOW |
                                        AUDIO_UAC_HEALTH_UNDERFLOW));
    r = sample(&monitor, false, 14u, 0u, 5u, 8u, 999999u);
    assert(r.flags == AUDIO_UAC_HEALTH_NONE);
    assert(r.active_data_loss_flags == AUDIO_UAC_HEALTH_NONE);
}

static void test_startup_grace_does_not_hide_sustained_underflow(void)
{
    audio_uac_health_monitor_t monitor = {0};
    (void)sample(&monitor, false, 10u, 0u, 0u, 0u, 100000u);
    audio_uac_health_result_t r = sample(
        &monitor, true, 11u, 100u, 0u, 0u, 100100u);
    assert(r.flags == AUDIO_UAC_HEALTH_PRESSURE_LOW);
    assert(r.active_data_loss_flags == AUDIO_UAC_HEALTH_NONE);
    r = sample(&monitor, true, 12u, 200u, 0u, 0u, 100200u);
    assert(r.flags == (AUDIO_UAC_HEALTH_PRESSURE_LOW |
                       AUDIO_UAC_HEALTH_UNDERFLOW));
    assert(r.delta_underflow_frames == 100u);
    assert(r.active_data_loss_flags == AUDIO_UAC_HEALTH_UNDERFLOW);
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
    audio_uac_health_monitor_t packets = {0};
    audio_uac_health_result_t p = audio_uac_health_sample(
        &packets, true, 1, 1024, 2048, 0, 0, 0, 100);
    assert(p.flags == 0); /* Earlier idle USB losses establish a baseline. */
    p = audio_uac_health_sample(&packets, true, 2, 1024, 2048, 0, 0, 0, 190);
    assert(p.flags == AUDIO_UAC_HEALTH_PACKET_LOSS);
    assert(p.delta_packet_lost_frames == 90);
    p = audio_uac_health_sample(&packets, true, 3, 1024, 2048, 0, 0, 0, 190);
    assert(p.flags == 0 && p.active_data_loss_flags == AUDIO_UAC_HEALTH_PACKET_LOSS);
    p = audio_uac_health_sample(&packets, false, 3, 1024, 2048, 0, 0, 0, 200);
    assert(p.flags == 0 && p.active_data_loss_flags == 0 && p.delta_packet_lost_frames == 0);
    test_ring_thresholds_and_states();
    test_pressure_and_active_data_loss();
    test_idle_and_playback_start_establish_baseline();
    test_startup_grace_does_not_hide_sustained_underflow();
    test_counter_reset_does_not_wrap();
    puts("audio_uac_health tests passed");
    return 0;
}
