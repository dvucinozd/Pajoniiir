#pragma once

/*
 * Single-producer/single-consumer block ring for the P4 master recorder.
 *
 * The producer is the real-time audio output task (audio_recorder_ring_push
 * from the master-mix hot path); the consumer is the low-priority writer task.
 * Storage is caller-provided (preallocated PSRAM on the firmware, a stack/heap
 * array in host tests), so this module never allocates and is portable.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max stereo frames per ring block — matches the audio output block
 * (AE_OUT_FRAMES). The producer pushes at most this many frames per block. */
#define AUDIO_RECORDER_BLOCK_FRAMES  256u
#define AUDIO_RECORDER_BLOCK_SAMPLES (AUDIO_RECORDER_BLOCK_FRAMES * 2u)  /* L+R */

/* One fixed-size ring slot: a rendered master block plus its metadata. */
typedef struct {
    uint32_t sample_rate;                            /* Hz for this block */
    uint32_t frames;                                 /* valid stereo frames */
    int16_t  samples[AUDIO_RECORDER_BLOCK_SAMPLES];  /* interleaved L/R */
} audio_recorder_block_t;

typedef struct {
    audio_recorder_block_t *slots;   /* caller-owned array of `capacity` blocks */
    uint32_t capacity;               /* number of slots (>= 2) */
    uint32_t write_seq;              /* producer-owned; published with RELEASE */
    uint32_t read_seq;               /* consumer-owned; published with RELEASE */
    uint32_t high_water;             /* max slots used observed (producer side) */
    uint32_t dropped_blocks;         /* full-ring drops (producer side) */
    uint64_t dropped_frames;         /* frames lost to drops (producer side) */
    uint64_t pushed_blocks;          /* total accepted blocks (producer side) */
} audio_recorder_ring_t;

/* Bind the ring to `capacity` caller-provided slots and reset all state. */
void audio_recorder_ring_init(audio_recorder_ring_t *ring,
                              audio_recorder_block_t *slots, uint32_t capacity);

/* Producer: copy one rendered master block in. Returns false without blocking
 * when the ring is full (drop counters advance). `frames` is clamped to
 * AUDIO_RECORDER_BLOCK_FRAMES. Performs no allocation, logging or file work. */
bool audio_recorder_ring_push(audio_recorder_ring_t *ring,
                              const int16_t *stereo, uint32_t frames,
                              uint32_t sample_rate);

/* Consumer: pointer to the next readable block, or NULL if empty. The pointer
 * is valid until the matching audio_recorder_ring_consume(). */
const audio_recorder_block_t *audio_recorder_ring_peek(const audio_recorder_ring_t *ring);

/* Consumer: release the block returned by the last peek. No-op if empty. */
void audio_recorder_ring_consume(audio_recorder_ring_t *ring);

/* Slots currently occupied (for diagnostics / backpressure decisions). */
uint32_t audio_recorder_ring_used(const audio_recorder_ring_t *ring);

#ifdef __cplusplus
}
#endif
