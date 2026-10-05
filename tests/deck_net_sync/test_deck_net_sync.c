/* JC1060 network beat sync (v304): a deck engaged on a DJ Link master
 * matches the master's tempo, snaps its bar once and then holds the phase by
 * pitch trims only. Simulates a 128 BPM master against a 125 BPM grid in
 * DECK_NET_SYNC_POLL_MS steps, with clock drift, rx jitter, a master that
 * stops and a master that jumps one beat. */
#include "deck_net_sync.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GRID_BEATS 2000u
#define LOCAL_BPM_X100 12500u
#define LOCAL_BEAT_MS 480u

static anlz_beat_t s_grid[GRID_BEATS];

static void make_grid(uint32_t first_ms)
{
    for (uint16_t i = 0; i < GRID_BEATS; i++) {
        s_grid[i].beat_phase = (uint16_t)(i % 4u + 1u);
        s_grid[i].bpm_x100 = LOCAL_BPM_X100;
        s_grid[i].time_ms = first_ms + (uint32_t)i * LOCAL_BEAT_MS;
    }
}

typedef struct {
    /* master */
    double period_ms;
    double beat_pos;          /* beats since start, master time */
    long   last_beat;
    bool   running;
    int    jitter_ms;         /* alternating +/- on every anchor */
    int    beat_shift;        /* added to the bar count (master jump) */
    deck_net_clock_t clock;
    /* local */
    double pos_ms;
    double drift;             /* local clock rate error, e.g. 300e-6 */
    bool   playing;
    float  pitch;
    unsigned seeks;
    deck_net_sync_t sync;
    deck_net_sync_out_t out;
    uint32_t now_ms;
} sim_t;

static void sim_init(sim_t *s, double master_bpm, double local_start_ms)
{
    memset(s, 0, sizeof(*s));
    s->period_ms = 60000.0 / master_bpm;
    s->last_beat = -1;
    s->running = true;
    s->pos_ms = local_start_ms;
    s->now_ms = 0xFFFFF000u;   /* crosses the uint32 wrap during the run */
    s->clock.player = 2;
}

/* The actual local beat position minus the master's, in beats (true error,
 * not what the controller estimates). */
static double true_error(const sim_t *s)
{
    double local_beats = (s->pos_ms - (double)s_grid[0].time_ms) / LOCAL_BEAT_MS;
    double d = fmod(s->beat_pos + s->beat_shift - local_beats, 4.0);
    if (d >= 2.0) d -= 4.0;
    if (d < -2.0) d += 4.0;
    return d;
}

static void sim_step(sim_t *s)
{
    const uint32_t dt = DECK_NET_SYNC_POLL_MS;
    s->now_ms += dt;
    if (s->running) {
        s->beat_pos += dt / s->period_ms;
        long beat = (long)floor(s->beat_pos);
        if (beat != s->last_beat) {
            s->last_beat = beat;
            double since_ms = (s->beat_pos - beat) * s->period_ms;
            int jitter = s->jitter_ms ? ((beat & 1) ? s->jitter_ms : -s->jitter_ms) : 0;
            s->clock.valid = true;
            s->clock.anchor_ms = s->now_ms - (uint32_t)lround(since_ms) + (uint32_t)jitter;
            s->clock.period_us = (uint32_t)lround(s->period_ms * 1000.0);
            s->clock.beat_in_bar = (uint8_t)(((beat + s->beat_shift) % 4 + 4) % 4 + 1);
        }
    }
    if (s->playing) {
        s->pos_ms += dt * (1.0 + s->pitch / 100.0) * (1.0 + s->drift);
    }
    deck_net_sync_local_t local = {
        .playing = s->playing,
        .position_ms = (uint32_t)lround(s->pos_ms),
        .beats = s_grid,
        .beat_count = GRID_BEATS,
    };
    deck_net_sync_step(&s->sync, &s->clock, &local, s->now_ms, &s->out);
    if (s->out.set_pitch) s->pitch = s->out.pitch_percent;
    if (s->out.seek) {
        s->pos_ms = s->out.seek_ms;
        s->seeks++;
    }
}

static void sim_run(sim_t *s, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += DECK_NET_SYNC_POLL_MS) sim_step(s);
}

static void test_clock_bar_position(void)
{
    deck_net_clock_t c = { .valid = true, .anchor_ms = 1000u, .period_us = 500000u,
                           .beat_in_bar = 3u };
    float bar = -1.0f;
    bool known = false;
    assert(deck_net_clock_bar_position(&c, 1250u, &bar, &known));
    assert(known && fabsf(bar - 2.5f) < 1e-4f);
    assert(deck_net_clock_beat_in_bar(&c, 1250u) == 3u);
    assert(deck_net_clock_beat_in_bar(&c, 1500u) == 4u);
    assert(deck_net_clock_beat_in_bar(&c, 2000u) == 1u);       /* wraps to the downbeat */
    assert(fabsf(deck_net_clock_bpm(&c) - 120.0f) < 1e-3f);
    /* Stale after 2.5 missed beats. */
    assert(!deck_net_clock_bar_position(&c, 1000u + 1300u, &bar, &known));
    assert(deck_net_clock_beat_in_bar(&c, 2300u) == 0u);
    /* Anchor across the uint32 wrap. */
    c.anchor_ms = 0xFFFFFF00u;
    assert(deck_net_clock_bar_position(&c, 0x00000100u, &bar, &known));
    assert(fabsf(bar - (2.0f + 512.0f / 500.0f)) < 1e-3f);
    /* Unknown bar: phase only. */
    c.beat_in_bar = 0u;
    assert(deck_net_clock_bar_position(&c, 0x00000100u, &bar, &known) && !known);
    assert(deck_net_clock_beat_in_bar(&c, 0x00000100u) == 0u);
    c.valid = false;
    assert(!deck_net_clock_bar_position(&c, 0x00000100u, &bar, &known));
}

static void test_local_position(void)
{
    make_grid(100u);
    float bar = -1.0f, len = 0.0f, bpm = 0.0f;
    bool known = false;
    assert(deck_net_sync_local_position(s_grid, GRID_BEATS, 100u + 480u * 6u + 240u,
                                        &bar, &known, &len, &bpm));
    assert(known && fabsf(bar - 2.5f) < 1e-4f && fabsf(len - 480.0f) < 1e-4f);
    assert(fabsf(bpm - 125.0f) < 1e-4f);
    /* Before the first beat: extrapolated back into the previous bar. */
    assert(deck_net_sync_local_position(s_grid, GRID_BEATS, 0u, &bar, &known, &len, &bpm));
    assert(fabsf(bar - (4.0f - 100.0f / 480.0f)) < 1e-4f);
    /* After the last beat: the last interval carries on. */
    uint32_t last = s_grid[GRID_BEATS - 1u].time_ms;
    assert(deck_net_sync_local_position(s_grid, GRID_BEATS, last + 480u, &bar, &known, &len, &bpm));
    assert(fabsf(bar - (float)((GRID_BEATS) % 4u)) < 1e-3f);
    assert(!deck_net_sync_local_position(s_grid, 1u, 500u, &bar, &known, &len, &bpm));
    assert(!deck_net_sync_local_position(NULL, 0u, 500u, &bar, &known, &len, &bpm));
}

static void test_engage_paused_matches_tempo_without_seek(void)
{
    make_grid(100u);
    sim_t s;
    sim_init(&s, 128.0, 10000.0);
    sim_run(&s, 1000u);                       /* master clock up */
    deck_net_sync_engage(&s.sync);
    sim_step(&s);
    assert(s.out.status == DECK_NET_SYNC_WAIT);
    assert(s.seeks == 0u);
    assert(fabsf(s.pitch - 2.4f) < 1e-3f);    /* 128 / 125 - 1 */
    sim_run(&s, 2000u);
    assert(s.seeks == 0u);                    /* a paused deck never jumps */
}

static void test_play_snaps_bar_then_locks(void)
{
    make_grid(100u);
    sim_t s;
    sim_init(&s, 128.0, 10000.0 + 170.0);     /* off grid, off bar */
    sim_run(&s, 1000u);
    deck_net_sync_engage(&s.sync);
    s.playing = true;
    sim_step(&s);
    assert(s.seeks == 1u && s.out.status == DECK_NET_SYNC_ALIGNING);
    /* The snap lands within one step of master motion, bar included. */
    assert(fabs(true_error(&s)) < 0.1);
    sim_run(&s, 8000u);
    assert(s.seeks == 1u);
    assert(s.out.status == DECK_NET_SYNC_LOCKED);
    assert(fabs(true_error(&s)) < 0.01);
    assert(fabsf(s.pitch - 2.4f) < 0.2f);
}

static void test_drift_and_jitter_stay_locked_by_trim(void)
{
    for (int direction=-1;direction<=1;direction+=2) {
    make_grid(100u);
    sim_t s;
    sim_init(&s, 126.5, 30000.0);
    s.drift = direction*400e-6;              /* positive and negative drift */
    s.jitter_ms = 3;
    sim_run(&s, 1000u);
    deck_net_sync_engage(&s.sync);
    s.playing = true;
    sim_run(&s, 5000u);
    float lo = 100.0f, hi = -100.0f;
    double worst = 0.0;
    for (uint32_t t = 0; t < 120000u; t += DECK_NET_SYNC_POLL_MS) {
        sim_step(&s);
        if (s.pitch < lo) lo = s.pitch;
        if (s.pitch > hi) hi = s.pitch;
        if (fabs(true_error(&s)) > worst) worst = fabs(true_error(&s));
    }
    assert(s.seeks == 1u);                    /* two minutes, no re-snap */
    assert(worst < 0.02);                     /* < ~10 ms */
    assert(hi - lo < 0.3f);                   /* trims stay small */
    assert(s.out.status == DECK_NET_SYNC_LOCKED);
    }
}

static void test_master_stops_holds_tempo(void)
{
    make_grid(100u);
    sim_t s;
    sim_init(&s, 128.0, 10000.0);
    sim_run(&s, 1000u);
    deck_net_sync_engage(&s.sync);
    s.playing = true;
    sim_run(&s, 6000u);
    float held = s.pitch;
    s.running = false;                        /* beats stop arriving */
    sim_run(&s, 3000u);
    assert(s.out.status == DECK_NET_SYNC_WAIT);
    assert(fabsf(s.pitch - held) < 0.05f);
    assert(s.seeks == 1u);
    assert(s.sync.engaged);
}

static void test_master_jump_requires_explicit_alignment(void)
{
    make_grid(100u);
    sim_t s;
    sim_init(&s, 128.0, 10000.0);
    sim_run(&s, 1000u);
    deck_net_sync_engage(&s.sync);
    s.playing = true;
    sim_run(&s, 6000u);
    assert(s.seeks == 1u);
    s.beat_shift = 1;                         /* master beat-jumps +1 */
    sim_run(&s, 6000u);
    assert(s.seeks == 1u);
    assert(s.out.status == DECK_NET_SYNC_ALIGNING);
    deck_net_sync_request_align(&s.sync);
    sim_run(&s, 6000u);
    assert(s.seeks == 2u);
    assert(fabs(true_error(&s)) < 0.01);
}

static void test_out_of_reach_and_disengage(void)
{
    make_grid(100u);
    sim_t s;
    sim_init(&s, 174.0, 10000.0);             /* 174 vs 125: +39%, out of reach */
    sim_run(&s, 1000u);
    deck_net_sync_engage(&s.sync);
    s.playing = true;
    sim_run(&s, 2000u);
    assert(s.out.status == DECK_NET_SYNC_WAIT);
    assert(s.pitch == 0.0f && s.seeks == 0u);

    deck_net_sync_disengage(&s.sync);
    sim_step(&s);
    assert(s.out.status == DECK_NET_SYNC_OFF && !s.out.set_pitch && !s.out.seek);
}

static void test_hold_keeps_pitch_no_seek(void)
{
    make_grid(100u);
    sim_t s;
    sim_init(&s, 128.0, 10000.0);
    sim_run(&s, 1000u);
    deck_net_sync_engage(&s.sync);
    deck_net_sync_local_t local = { .playing = true, .hold = true, .position_ms = 10000u,
                                    .beats = s_grid, .beat_count = GRID_BEATS };
    deck_net_sync_out_t out;
    deck_net_sync_step(&s.sync, &s.clock, &local, s.now_ms, &out);
    assert(!out.seek && out.status == DECK_NET_SYNC_WAIT);
    assert(!out.set_pitch);
}

static void test_latency_epoch_and_implicit_resume(void)
{
    make_grid(0);deck_net_sync_t s;deck_net_sync_engage(&s);
    deck_sink_latency_t c={.sink=1,.sample_rate=48000,.latency_us=20000,.measured=true};
    assert(deck_sink_latency_ms(&c,1,48000)==20);
    assert(deck_sink_latency_ms(&c,0,48000)==0 && deck_sink_latency_ms(&c,1,44100)==0);
    c.measured=false;assert(deck_sink_latency_ms(&c,1,48000)==0);
    deck_net_clock_t clock={.valid=true,.anchor_ms=1000,.period_us=480000,
        .beat_in_bar=1,.player=3,.source_epoch=20,.master_epoch=1};
    deck_net_sync_local_t local={.playing=true,.position_ms=40,.beats=s_grid,
        .beat_count=GRID_BEATS,.latency_ms=40};
    deck_net_sync_out_t out;
    deck_net_sync_step(&s,&clock,&local,1000,&out);
    assert(out.seek && out.seek_ms==40); /* audible position is zero */
    s.settling=false;deck_net_sync_step(&s,&clock,&local,1000,&out);
    assert(out.status==DECK_NET_SYNC_LOCKED && !out.seek);
    local.hold=true;deck_net_sync_request_align(&s);
    deck_net_sync_step(&s,&clock,&local,1000,&out);assert(!out.seek && !out.set_pitch);
    local.hold=false;local.playing=false;deck_net_sync_step(&s,&clock,&local,1000,&out);
    local.playing=true;deck_net_sync_step(&s,&clock,&local,1000,&out);
    assert(!out.seek); /* natural resume is not an explicit PLAY */
    deck_net_sync_request_align(&s);clock.source_epoch++;
    deck_net_sync_step(&s,&clock,&local,1000,&out);assert(!out.seek);
    deck_net_sync_request_align(&s);deck_net_sync_step(&s,&clock,&local,1000,&out);assert(out.seek);
    clock.valid=false;float held=s.pitch_percent;
    deck_net_sync_step(&s,&clock,&local,2000,&out);
    assert(out.status==DECK_NET_SYNC_WAIT && !out.set_pitch && s.pitch_percent==held);
    local.latency_ms=NAN;clock.valid=true;
    deck_net_sync_step(&s,&clock,&local,1000,&out);assert(out.status==DECK_NET_SYNC_WAIT && !out.seek);
    local.latency_ms=0;s_grid[0].beat_phase=0;local.position_ms=0;
    deck_net_sync_engage(&s);deck_net_sync_step(&s,&clock,&local,1000,&out);
    assert(out.status==DECK_NET_SYNC_WAIT && !out.seek); /* unknown bar */
}

int main(void)
{
    test_clock_bar_position();
    test_local_position();
    test_engage_paused_matches_tempo_without_seek();
    test_play_snaps_bar_then_locks();
    test_drift_and_jitter_stay_locked_by_trim();
    test_master_stops_holds_tempo();
    test_master_jump_requires_explicit_alignment();
    test_out_of_reach_and_disengage();
    test_hold_keeps_pitch_no_seek();
    test_latency_epoch_and_implicit_resume();
    printf("deck_net_sync: all tests passed\n");
    return 0;
}
