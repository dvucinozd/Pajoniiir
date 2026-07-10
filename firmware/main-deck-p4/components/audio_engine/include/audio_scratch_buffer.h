#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Per-deck scratch capture buffer (vinyl mode). A circular store of decoded
 * SOURCE-rate stereo int16 frames, tagged with the track position (ms) of the
 * most recently written frame, so a target position maps to a stored frame.
 *
 * Phase 2 (current): capture only — the decode task appends every produced
 * frame here in addition to the PCM ring, keeping a rolling window that ends
 * near (slightly ahead of) the playhead. Normal playback is unchanged.
 *
 * Phases 3-4 will read this bidirectionally at a jog-driven read head to
 * produce the audible scratch. See docs/VINYL_SCRATCH_PLAN.md.
 *
 * Frames are assumed contiguous at `sample_rate`: the writer appends
 * consecutive source frames, so the position of the frame `k` slots before the
 * newest is `newest_pos_ms - k*1000/sample_rate`. Callers reset the buffer on
 * any non-contiguous jump (a user seek flushes the ring, so it flushes this too)
 * to keep that assumption true.
 *
 * Backing storage is caller-owned (`frames`, capacity*2 interleaved int16), so
 * the component needs no allocation and is host-testable. Single-writer: the
 * decode task is the sole writer; Phase 4 adds the (single) output-task reader.
 */
typedef struct {
    int16_t *frames;        /* caller-owned, capacity*2 interleaved L,R */
    uint32_t capacity;      /* frames the store can hold */
    uint32_t write_index;   /* next slot to write [0,capacity) */
    uint32_t filled;        /* valid frames currently stored [0,capacity] */
    uint32_t sample_rate;   /* source frames/sec, for ms<->frame mapping */
    uint32_t newest_pos_ms; /* track position of the most recent frame */
    bool     newest_valid;  /* newest_pos_ms set since the last reset */
} audio_scratch_buffer_t;

/* Bind caller storage and reset. `capacity_frames` must be > 0 to be usable. */
void audio_scratch_buffer_init(audio_scratch_buffer_t *b, int16_t *storage,
                               uint32_t capacity_frames);

/* Drop all captured frames (keeps storage + sample_rate). Call on a user seek. */
void audio_scratch_buffer_reset(audio_scratch_buffer_t *b);

/* Set the source sample rate used for ms<->frame mapping. */
void audio_scratch_buffer_set_sample_rate(audio_scratch_buffer_t *b,
                                          uint32_t sample_rate);

/* Append one stereo frame (overwrites the oldest once full). */
void audio_scratch_buffer_push(audio_scratch_buffer_t *b, int16_t left,
                               int16_t right);

/* Record the track position (ms) of the most recently pushed frame. */
void audio_scratch_buffer_mark_newest_ms(audio_scratch_buffer_t *b,
                                         uint32_t pos_ms);

/* Number of valid frames currently stored. */
uint32_t audio_scratch_buffer_used(const audio_scratch_buffer_t *b);

/* Map a track position (ms) to a stored frame index.
 * Returns false if the buffer is empty/unset, the position is in the future
 * (newer than the newest frame), or older than the buffered window. */
bool audio_scratch_buffer_index_for_ms(const audio_scratch_buffer_t *b,
                                       uint32_t pos_ms, uint32_t *out_index);

/* Read the stereo frame at an absolute store index [0,capacity).
 * Returns false on a null buffer/output or an out-of-range index. Whether the
 * index still holds live window data is the caller's concern (index_for_ms). */
bool audio_scratch_buffer_read(const audio_scratch_buffer_t *b, uint32_t index,
                               int16_t *out_left, int16_t *out_right);
