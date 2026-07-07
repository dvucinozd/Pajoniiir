#include "s3_debug_ap.h"

#include <stdio.h>
#include <string.h>

void s3_debug_log_ring_init(s3_debug_log_ring_t *ring)
{
    if (!ring) {
        return;
    }
    memset(ring, 0, sizeof(*ring));
}

void s3_debug_log_ring_append(s3_debug_log_ring_t *ring, const char *text)
{
    if (!ring || !text) {
        return;
    }

    snprintf(ring->lines[ring->next_index], S3_DEBUG_LOG_LINE_MAX, "%s", text);
    ring->next_seq++;
    ring->next_index = (uint8_t)((ring->next_index + 1u) % S3_DEBUG_LOG_RING_LINES);
    if (ring->count < S3_DEBUG_LOG_RING_LINES) {
        ring->count++;
    }
}

size_t s3_debug_log_ring_snapshot(const s3_debug_log_ring_t *ring,
                                  char *out,
                                  size_t out_size,
                                  uint32_t after_seq)
{
    if (!ring || !out || out_size == 0) {
        return 0;
    }

    size_t used = 0;
    out[0] = '\0';

    uint32_t first_seq = ring->next_seq - ring->count + 1u;
    for (uint8_t i = 0; i < ring->count; i++) {
        uint32_t seq = first_seq + i;
        if (seq <= after_seq) {
            continue;
        }

        uint8_t index = (uint8_t)((ring->next_index + S3_DEBUG_LOG_RING_LINES -
                                   ring->count + i) % S3_DEBUG_LOG_RING_LINES);
        const char *line = ring->lines[index];
        size_t len = strlen(line);
        if (used + len >= out_size) {
            len = out_size - used - 1u;
        }
        memcpy(out + used, line, len);
        used += len;
        out[used] = '\0';
        if (used + 1u >= out_size) {
            break;
        }
    }
    return used;
}
