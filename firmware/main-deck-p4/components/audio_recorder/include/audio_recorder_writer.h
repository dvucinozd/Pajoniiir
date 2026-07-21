#pragma once

/*
 * Consumer-side drain step for the P4 master recorder.
 *
 * Pulls rendered blocks off the SPSC ring and hands each block's PCM payload to
 * a storage sink. The sink is injectable so this core is host-testable with a
 * fake buffer/failure sink; on the firmware the sink is a bounded FAT write.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_recorder_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Storage sink: write exactly `len` bytes; return `len` on success or a
 * negative value on failure. `ctx` is opaque caller state (e.g. an open file). */
typedef int (*audio_recorder_sink_fn)(void *ctx, const void *data, size_t len);

typedef struct {
    uint32_t blocks_written;
    uint64_t bytes_written;
    bool     sink_error;   /* true if the sink failed during this drain */
} audio_recorder_drain_result_t;

/* Drain up to `max_blocks` from `ring`, writing each block's PCM payload
 * (frames * AUDIO_RECORDER_WAV_FRAME_BYTES) to `sink`. Stops on the first sink
 * error, leaving the failing block unconsumed so the caller can react (finalize
 * or enter ERROR). Returns the accumulated counters for this call. */
audio_recorder_drain_result_t
audio_recorder_writer_drain(audio_recorder_ring_t *ring,
                            audio_recorder_sink_fn sink, void *ctx,
                            uint32_t max_blocks);

#ifdef __cplusplus
}
#endif
