#pragma once
/* Adapted from kayrozen/Pajoniiir 428b97dd, audio_cue_preroll (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t frames;
    bool pending;
} audio_cue_preroll_t;

/* max_pre_frames must leave one complete decoder batch below the forward cap. */
uint32_t audio_cue_preroll_arm(audio_cue_preroll_t *p, bool arm,
                               uint32_t target_ms, uint32_t sample_rate,
                               uint32_t max_pre_frames);
/* At EOF, discard all available history if the cue lies beyond the source.
 * This does not change the published transport position. */
bool audio_cue_preroll_publish_point(const audio_cue_preroll_t *p,
                                     uint64_t write_seq, bool eof,
                                     uint64_t *playhead);
