#include "audio_output_sink.h"

static void increment(uint32_t *value)
{
    if (value) (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

audio_output_sink_result_t audio_output_sink_write_all(
    audio_output_sink_write_fn write_fn,
    void *ctx,
    const void *data,
    size_t bytes,
    uint32_t timeout_ticks,
    uint32_t max_calls,
    audio_output_sink_stats_t *stats)
{
    if (!write_fn || !data || bytes == 0u || timeout_ticks == 0u ||
        max_calls == 0u) {
        increment(stats ? &stats->errors : NULL);
        increment(stats ? &stats->failed_blocks : NULL);
        return AUDIO_OUTPUT_SINK_ERROR;
    }

    const uint8_t *cursor = (const uint8_t *)data;
    size_t remaining = bytes;
    for (uint32_t call = 0u; call < max_calls; ++call) {
        size_t written = 0u;
        increment(stats ? &stats->calls : NULL);
        audio_output_sink_result_t result = write_fn(
            ctx, cursor, remaining, &written, timeout_ticks);
        if (result == AUDIO_OUTPUT_SINK_TIMEOUT) {
            increment(stats ? &stats->timeouts : NULL);
            increment(stats ? &stats->failed_blocks : NULL);
            return result;
        }
        if (result != AUDIO_OUTPUT_SINK_OK || written > remaining) {
            increment(stats ? &stats->errors : NULL);
            increment(stats ? &stats->failed_blocks : NULL);
            return AUDIO_OUTPUT_SINK_ERROR;
        }
        if (written == remaining) return AUDIO_OUTPUT_SINK_OK;

        increment(stats ? &stats->short_writes : NULL);
        if (written == 0u) {
            increment(stats ? &stats->failed_blocks : NULL);
            return AUDIO_OUTPUT_SINK_ERROR;
        }
        cursor += written;
        remaining -= written;
    }

    increment(stats ? &stats->failed_blocks : NULL);
    return AUDIO_OUTPUT_SINK_ERROR;
}

void audio_output_sink_stats_snapshot(const audio_output_sink_stats_t *stats,
                                      audio_output_sink_stats_t *out)
{
    if (!out) return;
    if (!stats) {
        *out = (audio_output_sink_stats_t) { 0 };
        return;
    }
    out->calls = __atomic_load_n(&stats->calls, __ATOMIC_RELAXED);
    out->short_writes = __atomic_load_n(&stats->short_writes, __ATOMIC_RELAXED);
    out->timeouts = __atomic_load_n(&stats->timeouts, __ATOMIC_RELAXED);
    out->errors = __atomic_load_n(&stats->errors, __ATOMIC_RELAXED);
    out->failed_blocks = __atomic_load_n(&stats->failed_blocks, __ATOMIC_RELAXED);
}
