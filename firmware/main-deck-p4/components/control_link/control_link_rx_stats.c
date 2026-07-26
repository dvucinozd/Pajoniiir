#include "control_link_rx_stats.h"

#include <string.h>

void control_link_rx_stats_reset(control_link_rx_stats_t *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

void control_link_rx_stats_record_frame(control_link_rx_stats_t *stats,
                                        uint8_t sequence,
                                        bool bulk)
{
    if (!stats) {
        return;
    }
    if (stats->sequence_valid) {
        uint8_t expected = (uint8_t)(stats->last_sequence + 1u);
        if (sequence != expected) {
            stats->sequence_gaps++;
        }
    }
    stats->last_sequence = sequence;
    stats->sequence_valid = true;
    stats->rx_frames++;
    if (bulk) {
        stats->bulk_frames++;
    }
}

void control_link_rx_stats_record_error(control_link_rx_stats_t *stats,
                                        bool bulk)
{
    if (!stats) {
        return;
    }
    if (bulk) {
        stats->bulk_crc_errors++;
    } else {
        stats->event_checksum_errors++;
    }
}
