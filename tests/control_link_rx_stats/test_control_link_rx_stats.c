#include "control_link_rx_stats.h"

#include <assert.h>
#include <stdio.h>

static void test_first_frame_and_contiguous_sequence(void)
{
    control_link_rx_stats_t stats;
    control_link_rx_stats_reset(&stats);

    control_link_rx_stats_record_frame(&stats, 254u, false);
    control_link_rx_stats_record_frame(&stats, 255u, true);
    control_link_rx_stats_record_frame(&stats, 0u, false);

    assert(stats.rx_frames == 3u);
    assert(stats.bulk_frames == 1u);
    assert(stats.sequence_gaps == 0u);
    assert(stats.last_sequence == 0u);
    assert(stats.sequence_valid);
}

static void test_gap_and_error_classes_are_counted(void)
{
    control_link_rx_stats_t stats = {0};

    control_link_rx_stats_record_frame(&stats, 10u, false);
    control_link_rx_stats_record_error(&stats, false);
    control_link_rx_stats_record_frame(&stats, 12u, true);
    control_link_rx_stats_record_error(&stats, true);
    control_link_rx_stats_record_error(&stats, true);

    assert(stats.rx_frames == 2u);
    assert(stats.sequence_gaps == 1u);
    assert(stats.event_checksum_errors == 1u);
    assert(stats.bulk_frames == 1u);
    assert(stats.bulk_crc_errors == 2u);
    assert(stats.last_sequence == 12u);
}

static void test_null_calls_are_safe(void)
{
    control_link_rx_stats_reset(NULL);
    control_link_rx_stats_record_frame(NULL, 1u, false);
    control_link_rx_stats_record_error(NULL, true);
}

int main(void)
{
    test_first_frame_and_contiguous_sequence();
    test_gap_and_error_classes_are_counted();
    test_null_calls_are_safe();
    puts("control_link_rx_stats tests passed");
    return 0;
}
