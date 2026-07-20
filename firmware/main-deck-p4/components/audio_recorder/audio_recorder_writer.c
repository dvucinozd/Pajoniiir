#include "audio_recorder_writer.h"

#include "audio_recorder_wav.h"   /* AUDIO_RECORDER_WAV_FRAME_BYTES */

audio_recorder_drain_result_t
audio_recorder_writer_drain(audio_recorder_ring_t *ring,
                            audio_recorder_sink_fn sink, void *ctx,
                            uint32_t max_blocks)
{
    audio_recorder_drain_result_t res = { 0u, 0u, false };
    if (!ring || !sink) {
        return res;
    }

    for (uint32_t i = 0u; i < max_blocks; i++) {
        const audio_recorder_block_t *blk = audio_recorder_ring_peek(ring);
        if (!blk) {
            break;   /* ring empty */
        }
        size_t len = (size_t)blk->frames * AUDIO_RECORDER_WAV_FRAME_BYTES;
        int n = sink(ctx, blk->samples, len);
        if (n < 0 || (size_t)n != len) {
            res.sink_error = true;
            break;   /* leave the failing block unconsumed */
        }
        audio_recorder_ring_consume(ring);
        res.blocks_written++;
        res.bytes_written += len;
    }
    return res;
}
