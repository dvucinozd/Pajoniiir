#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Per-deck scratch capture buffer (vinyl mode). A circular store of decoded
 * SOURCE-rate stereo int16 frames.
 *
 * The decode path is the sole writer and advances write_index/filled while it
 * publishes canonical timeline data. The output task is the sole reader and
 * walks the retained window with audio_scratch_buffer_read_frame_back(). This
 * component therefore owns reset/generation/accessor semantics, not a second
 * public writer or millisecond-index API.
 *
 * Backing storage is caller-owned (frames, capacity*2 interleaved int16), so
 * the component needs no allocation and remains host-testable.
 */
typedef struct {
    int16_t *frames;        /* caller-owned, capacity*2 interleaved L,R */
    uint32_t capacity;      /* frames the store can hold */
    uint32_t write_index;   /* next slot to write [0,capacity) */
    uint32_t filled;        /* valid frames currently stored [0,capacity] */
    uint32_t sample_rate;   /* source frames/sec */
    uint32_t newest_pos_ms; /* track position of the most recent frame */
    uint32_t generation;    /* increments whenever timeline continuity resets */
    bool     newest_valid;  /* newest_pos_ms set since the last reset */
} audio_scratch_buffer_t;

/* Bind caller storage and reset. capacity_frames must be > 0 to be usable. */
void audio_scratch_buffer_init(audio_scratch_buffer_t *b, int16_t *storage,
                               uint32_t capacity_frames);

/* Drop all captured frames while keeping storage and sample_rate. */
void audio_scratch_buffer_reset(audio_scratch_buffer_t *b);

/* Publish source-rate/timeline metadata used by the scratch transport. */
void audio_scratch_buffer_set_sample_rate(audio_scratch_buffer_t *b,
                                          uint32_t sample_rate);
void audio_scratch_buffer_mark_newest_ms(audio_scratch_buffer_t *b,
                                         uint32_t pos_ms);

/* Snapshot accessors. */
uint32_t audio_scratch_buffer_used(const audio_scratch_buffer_t *b);
uint32_t audio_scratch_buffer_generation(const audio_scratch_buffer_t *b);

/* Read the stereo frame frames_back slots before the newest (0 = newest).
 * Returns false when frames_back is outside the currently retained window. */
bool audio_scratch_buffer_read_frame_back(const audio_scratch_buffer_t *b,
                                          uint32_t frames_back,
                                          int16_t *out_left, int16_t *out_right);
