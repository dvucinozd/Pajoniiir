#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"

/*
 * Canonical decoded-PCM timeline.
 *
 * `write_seq` is the decoder cursor (next absolute frame to write) and
 * `play_seq` is the normal-output cursor (next frame to play). Consumed frames
 * remain addressable as history until capacity pressure overwrites them, while
 * frames between play_seq and write_seq form scratch forward runway.
 *
 * Caller owns `frames` (capacity*2 interleaved stereo int16), normally PSRAM.
 * Single producer writes; the output task owns play_seq. Scratch may read any
 * immutable sequence inside [oldest_seq, write_seq). Integration must freeze the
 * producer before allowing the scratch reader to use a stable window.
 */
typedef struct {
    int16_t *frames;
    uint32_t capacity;
    /* Low 32 bits stay in the per-frame SPSC path: ESP32-P4 is RV32, so 64-bit
     * atomics there would require expensive helpers. Epochs advance only when
     * a low cursor wraps; a per-cursor version makes the rare epoch/low update
     * coherent for public 64-bit snapshots. Retained spans are bounded by
     * capacity (< 2^31), so hot-path availability uses unsigned distance. */
    uint32_t oldest_seq;
    uint32_t play_seq;
    uint32_t write_seq;
    uint32_t oldest_epoch;
    uint32_t play_epoch;
    uint32_t write_epoch;
    uint32_t oldest_version;
    uint32_t play_version;
    uint32_t write_version;
    uint32_t play_index;
    uint32_t write_index;
    uint32_t generation;
} audio_pcm_timeline_t;

void audio_pcm_timeline_init(audio_pcm_timeline_t *t, int16_t *storage,
                             uint32_t capacity_frames);
void audio_pcm_timeline_reset(audio_pcm_timeline_t *t);

/* Append one decoded frame. Returns false rather than overwrite the current or
 * future playback cursor. Old consumed history is evicted automatically. */
bool audio_pcm_timeline_push(audio_pcm_timeline_t *t, int16_t left, int16_t right);

/* Pop the next normal-playback frame. */
bool audio_pcm_timeline_pop(audio_pcm_timeline_t *t, audio_mixer_frame_t *out);

/* Withdraw up to `frames` of the most recently pushed frames that normal
 * playback has not reached yet. Returns how many were actually withdrawn
 * (clamped to the forward runway, write_seq - play_seq). Producer-side only,
 * and the consumer must be excluded while it runs. */
uint32_t audio_pcm_timeline_drop_newest(audio_pcm_timeline_t *t, uint32_t frames);

/* Random-access read by monotonic 64-bit frame sequence. */
bool audio_pcm_timeline_read(const audio_pcm_timeline_t *t, uint64_t seq,
                             audio_mixer_frame_t *out);

/* Reposition normal playback to any retained frame, or write_seq (end). */
bool audio_pcm_timeline_set_playhead(audio_pcm_timeline_t *t, uint64_t seq);

/* Reposition using the scratch coordinate (0 = newest retained frame). */
bool audio_pcm_timeline_set_playhead_frames_back(audio_pcm_timeline_t *t,
                                                 uint32_t frames_back);

uint64_t audio_pcm_timeline_oldest_seq(const audio_pcm_timeline_t *t);
uint64_t audio_pcm_timeline_play_seq(const audio_pcm_timeline_t *t);
uint64_t audio_pcm_timeline_write_seq(const audio_pcm_timeline_t *t);
uint32_t audio_pcm_timeline_history_frames(const audio_pcm_timeline_t *t);
uint32_t audio_pcm_timeline_future_frames(const audio_pcm_timeline_t *t);
uint32_t audio_pcm_timeline_used_frames(const audio_pcm_timeline_t *t);
uint32_t audio_pcm_timeline_generation(const audio_pcm_timeline_t *t);
