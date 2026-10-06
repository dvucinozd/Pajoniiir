// Derived from kayrozen/Pajoniiir 428b97dd4a175f03d3a172c8db9c4d5ed94195fb.
// MIT, Copyright (c) 2024 The Pajoniiir Contributors; see root LICENSE.
// P4 owns the audible timeline.
//
// A deck whose SYNC is engaged while a DJ Link tempo master plays follows the
// master's received beats: its tempo is matched to the master's effective BPM
// and its phase is held on the master's beats by small pitch trims (a
// proportional loop, never a resampler restart). A seek is used only to snap
// the bar ONLY on explicit SYNC engage or PLAY. Never periodically re-snap.
//
// Plain data, no RTOS, no heap: the deck task owns one deck_net_sync_t per
// deck and applies output through the nonblocking, session-fenced
// audio_engine_deck_apply_network(). The local deck stays authoritative for
// its own playback; the network is only the reference clock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rekordbox_anlz.h"
#include "deck_net_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DECK_NET_SYNC_POLL_MS        40u
#define DECK_NET_SYNC_STALE_BEATS    2.5f   /* master clock dead after this many missed beats */
#define DECK_NET_SYNC_MAX_PERCENT    20.0f  /* same reach as local BEAT SYNC */
#define DECK_NET_SYNC_TRIM_GAIN      10.0f  /* % pitch per beat of phase error */
#define DECK_NET_SYNC_TRIM_MAX       1.0f   /* % pitch, phase trim on top of the BPM match */
#define DECK_NET_SYNC_DEADBAND       0.004f /* beats (~2 ms at 120 BPM): no trim inside */
#define DECK_NET_SYNC_FILTER         0.25f  /* phase error smoothing per step */
#define DECK_NET_SYNC_LOCK_BEATS     0.05f  /* |phase error| under this = LOCKED */
#define DECK_NET_SYNC_SETTLE_MS      300u   /* no trim while a snap seek lands */
#define DECK_NET_SYNC_PITCH_EPSILON  0.001f /* % pitch: smaller changes are not re-applied */

typedef struct {
    bool     engaged;
    bool     was_playing;
    bool     need_snap;
    bool     have_pitch;
    float    pitch_percent;     /* last value handed to the engine */
    bool     filt_valid;
    float    err_filt;          /* beats, master minus local */
    bool     far;
    uint32_t far_since_ms;
    bool     settling;
    uint32_t settle_until_ms;
    bool     snapped;
    uint32_t last_snap_ms;
    deck_net_sync_status_t status;
    uint64_t source_epoch, master_epoch;
    uint8_t player;
} deck_net_sync_t;

typedef struct {
    bool     playing;
    bool     hold;              /* jog touched / scratching: keep the pitch */
    uint32_t position_ms;       /* playhead, track time */
    const anlz_beat_t *beats;   /* loaded track's grid, NULL = none */
    uint16_t beat_count;
    float latency_ms;         /* measured sink latency, 0 when uncalibrated */
    float current_pitch;      /* converts wall latency to source-track time */
} deck_net_sync_local_t;

typedef struct {
    deck_net_sync_status_t status;
    bool     set_pitch;
    float    pitch_percent;
    bool     seek;
    uint32_t seek_ms;
    float    phase_error;       /* filtered, beats; 0 when unknown */
} deck_net_sync_out_t;

void deck_net_sync_engage(deck_net_sync_t *s);
void deck_net_sync_disengage(deck_net_sync_t *s);
void deck_net_sync_request_align(deck_net_sync_t *s); /* explicit PLAY only */

/* Calibration is never carried across a sink or sample-rate change. */
float deck_sink_latency_ms(const deck_sink_latency_t *c, uint8_t sink, uint32_t rate);

/* Master position in the bar at now_ms, 0..4 beats (0 = downbeat). False when
 * the clock is invalid or no beat came for DECK_NET_SYNC_STALE_BEATS. With
 * beat_in_bar unknown the integer part is meaningless; *bar_known says so. */
bool deck_net_clock_bar_position(const deck_net_clock_t *clock, uint32_t now_ms,
                                 float *out_bar, bool *bar_known);
/* Beat in bar 1..4 at now_ms, 0 = unknown or stale. */
uint8_t deck_net_clock_beat_in_bar(const deck_net_clock_t *clock, uint32_t now_ms);
float deck_net_clock_bpm(const deck_net_clock_t *clock);

/* Local position in the bar from the grid, 0..4 beats; the grid's beat length
 * and BPM at that point. Extrapolates before the first and after the last
 * beat. False without at least two beats. */
bool deck_net_sync_local_position(const anlz_beat_t *beats, uint16_t beat_count,
                                  uint32_t position_ms, float *out_bar,
                                  bool *bar_known, float *beat_len_ms, float *bpm);

void deck_net_sync_step(deck_net_sync_t *s, const deck_net_clock_t *clock,
                        const deck_net_sync_local_t *local, uint32_t now_ms,
                        deck_net_sync_out_t *out);

#ifdef __cplusplus
}
#endif
