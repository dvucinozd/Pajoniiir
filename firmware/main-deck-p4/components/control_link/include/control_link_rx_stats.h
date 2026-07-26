#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Receive-side health for the shared S3 -> P4 UART sequence.
 *
 * The S3 uses one rolling sequence for valid 0xA5 event and 0xA6 bulk frames,
 * so a discontinuity is counted across both frame classes. The first valid
 * frame establishes the baseline and never counts as a gap.
 */
typedef struct {
    uint32_t rx_frames;
    uint32_t sequence_gaps;
    uint32_t event_checksum_errors;
    uint32_t bulk_frames;
    uint32_t bulk_crc_errors;
    uint8_t last_sequence;
    bool sequence_valid;
} control_link_rx_stats_t;

void control_link_rx_stats_reset(control_link_rx_stats_t *stats);
void control_link_rx_stats_record_frame(control_link_rx_stats_t *stats,
                                        uint8_t sequence,
                                        bool bulk);
void control_link_rx_stats_record_error(control_link_rx_stats_t *stats,
                                        bool bulk);

#ifdef __cplusplus
}
#endif
