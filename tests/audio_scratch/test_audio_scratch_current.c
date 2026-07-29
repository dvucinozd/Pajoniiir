#include <assert.h>
#include <stdint.h>
#include "audio_scratch_buffer.h"

/* Mirrors the sole production decode-writer's circular-buffer bookkeeping.
 * The removed public push helper was test-only; scratch rendering should be
 * exercised through the active frames-back reader API. */
static void test_audio_scratch_writer_push(audio_scratch_buffer_t *buffer,
                                           int16_t left,
                                           int16_t right)
{
    assert(buffer && buffer->frames && buffer->capacity > 0u);
    const uint32_t index = buffer->write_index;
    buffer->frames[index * 2u] = left;
    buffer->frames[index * 2u + 1u] = right;
    buffer->write_index = index + 1u < buffer->capacity ? index + 1u : 0u;
    if (buffer->filled < buffer->capacity) {
        buffer->filled++;
    }
}

#define audio_scratch_buffer_push test_audio_scratch_writer_push
#include "test_audio_scratch.c"
#undef audio_scratch_buffer_push
