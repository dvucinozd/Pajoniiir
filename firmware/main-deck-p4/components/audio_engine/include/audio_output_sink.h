#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_OUTPUT_SINK_OK = 0,
    AUDIO_OUTPUT_SINK_TIMEOUT,
    AUDIO_OUTPUT_SINK_ERROR,
} audio_output_sink_result_t;

typedef audio_output_sink_result_t (*audio_output_sink_write_fn)(
    void *ctx,
    const uint8_t *data,
    size_t bytes,
    size_t *written,
    uint32_t timeout_ticks);

typedef struct {
    uint32_t calls;
    uint32_t short_writes;
    uint32_t timeouts;
    uint32_t errors;
    uint32_t failed_blocks;
} audio_output_sink_stats_t;

audio_output_sink_result_t audio_output_sink_write_all(
    audio_output_sink_write_fn write_fn,
    void *ctx,
    const void *data,
    size_t bytes,
    uint32_t timeout_ticks,
    uint32_t max_calls,
    audio_output_sink_stats_t *stats);

void audio_output_sink_stats_snapshot(const audio_output_sink_stats_t *stats,
                                      audio_output_sink_stats_t *out);
