/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v274: loop X2 / /2 (and the IN/OUT jog, and exit) while a loop plays.
 *
 * v273 hardware: after X2 the loop still went back at its old end once, then
 * followed the new length, the waveform half a loop off the audio. The
 * decoder runs ~2 s ahead of the playhead, so the ring already holds the
 * wrap at the old end (old end -> start) when the loop changes; setting the
 * new bounds only affected the next wrap. Upstream main-deck-p4 does the
 * same (audio_engine_deck_set_loop only stores the bounds).
 *
 * The ring is right up to the first point where the old and new loops part:
 * the old end when the loop grows (or is exited), the new end when it
 * shrinks. The frames past that point are withdrawn, the playhead keeps
 * playing, and the decoder restarts there: at the old end (exactly, the
 * track continues) or at the start (a wrap). Where the decoded frames sit
 * follows from the decoder cursor and the ring fill: the frames since its
 * last seek and, when that seek was a loop wrap, whole passes of the old loop
 * before them (a 1/32 beat loop wraps dozens of times in the ring).
 */

typedef struct {
    uint32_t old_start_ms;        /* loop the ring was decoded against */
    uint32_t old_end_ms;
    uint32_t new_start_ms;
    uint32_t new_end_ms;          /* UINT32_MAX: the loop was exited */
    uint32_t seek_base_ms;        /* decoder cursor: last seek target */
    uint64_t frames_since_seek;   /* frames pushed since */
    bool since_wrap;              /* that seek was a wrap to old_start_ms */
    uint32_t ring_frames;         /* decoded, not played yet */
    uint32_t sample_rate;
} audio_loop_resize_in_t;

typedef struct {
    bool cut;                     /* false: the ring is right as it is */
    uint32_t drop_frames;         /* newest frames to withdraw */
    uint32_t seek_ms;             /* where the decoder restarts */
    bool exact;                   /* continue the track (skip to seek_ms) rather than wrap */
} audio_loop_resize_plan_t;

/* No cut when the parting point is already played: the playhead is past
 * the new end, see audio_loop_resize_jump_ms. */
audio_loop_resize_plan_t audio_loop_resize_plan(const audio_loop_resize_in_t *in);

/* A shrunk loop whose end is at or behind the playhead: the same phase
 * inside the new loop, as a CDJ does. False when the playhead is inside it
 * (or before its start). */
bool audio_loop_resize_jump_ms(uint32_t position_ms, uint32_t start_ms, uint32_t end_ms,
                               uint32_t *target_ms);

#ifdef __cplusplus
}
#endif
