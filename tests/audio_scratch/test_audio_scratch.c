#include "audio_scratch.h"
#include "audio_scratch_buffer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Window of 10 frames. Values encode order: the i-th pushed frame is
 * (i*100, -i*100), so after all pushes frame `k` back from the newest holds
 * ((N-1-k)*100). Newest (back 0) = 900, oldest (back 9) = 0. */
#define N 10
static int16_t g_store[N * 2];

#define HOLD_FOREVER 1000000u

/* Deterministic per-sample config: a 1-sample rate window + instant slew means
 * one jog + one render sets the velocity to exactly ticks*frames_per_tick. */
static void config_instant(audio_scratch_t *s, float frames_per_tick,
                           float velocity_max, uint32_t hold_windows)
{
    audio_scratch_config(s, frames_per_tick, 1u, 1.0f, velocity_max, hold_windows);
}

static void fill_window(audio_scratch_buffer_t *b)
{
    audio_scratch_buffer_init(b, g_store, N);
    audio_scratch_buffer_set_sample_rate(b, 1000u);
    for (int i = 0; i < N; i++) {
        audio_scratch_buffer_push(b, (int16_t)(i * 100), (int16_t)(-i * 100));
    }
    audio_scratch_buffer_mark_newest_ms(b, 1000u);
}

static bool feq(float a, float b) { return fabsf(a - b) < 1e-3f; }

static void test_inactive_and_stopped_are_silent(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);

    int16_t l = 7, r = 7;
    /* Not seeded -> inactive -> silence. */
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(l == 0 && r == 0);

    /* Seeded but no jog motion (platter held still) -> silence. */
    audio_scratch_seed(&s, 4.0f);
    assert(audio_scratch_is_active(&s));
    l = 7; r = 7;
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(l == 0 && r == 0);
}

static void test_invalid_configuration_is_sanitized(void)
{
    audio_scratch_t s;
    audio_scratch_init(&s);
    audio_scratch_config(&s, NAN, 0u, INFINITY, -1.0f, 3u);
    assert(s.frames_per_tick == AUDIO_SCRATCH_DEFAULT_FRAMES_PER_TICK);
    assert(s.rate_window_samples == 1u);
    assert(s.slew_coef == AUDIO_SCRATCH_DEFAULT_SLEW_COEF);
    assert(s.velocity_max == AUDIO_SCRATCH_DEFAULT_VELOCITY_MAX);

    audio_scratch_seed(&s, NAN);
    assert(s.head_back == 0.0f);
    assert(audio_scratch_track_position_ms(1234u, INFINITY, 44100u,
                                           false, 0u, 0u) == 1234u);
}

static void test_forward_playback_ascends_toward_newest(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 5.0f);
    audio_scratch_jog(&s, 1);   /* velocity = +1 (forward, toward newer) */

    int16_t l = 0, r = 0;
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 400 && r == -400);   /* back 5 -> (9-5)*100 */
    assert(audio_scratch_render(&s, &b, &l, &r));  /* held velocity */
    assert(l == 500);                /* back 4 */
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 600);                /* back 3 */
}

static void test_reverse_playback_descends_toward_oldest(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 4.0f);
    audio_scratch_jog(&s, -1);  /* velocity = -1 (reverse, toward older) */

    int16_t l = 0, r = 0;
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 500);   /* back 4 */
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 400);   /* back 5 */
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 300);   /* back 6 */
}

static void test_linear_interpolation(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 0.5f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 3.5f);
    audio_scratch_jog(&s, 1);   /* velocity = +0.5 */

    int16_t l = 0, r = 0;
    assert(audio_scratch_render(&s, &b, &l, &r));
    /* Halfway between back 3 (600) and back 4 (500) -> 550. */
    assert(l == 550 && r == -550);
}

static void test_forward_past_newest_is_silent(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 0.0f);   /* at the newest frame */
    audio_scratch_jog(&s, 1);       /* forward -> past the window */

    int16_t l = 5, r = 5;
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(l == 0 && r == 0);
}

static void test_reverse_past_oldest_is_silent(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, (float)(N - 1));  /* at the oldest frame */
    audio_scratch_jog(&s, -1);               /* reverse -> past the window */

    int16_t l = 5, r = 5;
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(l == 0 && r == 0);
}

static void test_reversal_walks_head_back_into_window(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 0.0f);
    audio_scratch_jog(&s, 1);   /* forward past newest -> silent, head pinned 0 */

    int16_t l = 0, r = 0;
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(feq(audio_scratch_head_back(&s), 0.0f));

    /* Reverse: fresh ticks re-estimate the rate -> velocity -2, head walks
     * back into the window and the newest frame is audible again. */
    audio_scratch_jog(&s, -2);
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 900);                              /* back 0 = newest */
    assert(feq(audio_scratch_head_back(&s), 2.0f));
}

static void test_window_edge_latches_silent_until_inward_reversal(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 0.0f);

    int16_t l = 7, r = 7;
    audio_scratch_jog(&s, 4);
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(s.edge_latch == 1);
    assert(s.edge_hits == 1u);
    assert(feq(s.velocity, 0.0f));

    /* Repeated outward motion remains a clean stop, never clamp chatter. */
    for (int i = 0; i < 8; i++) {
        audio_scratch_jog(&s, 1);
        l = 7; r = 7;
        assert(!audio_scratch_render(&s, &b, &l, &r));
        assert(l == 0 && r == 0);
        assert(feq(audio_scratch_head_back(&s), 0.0f));
        assert(feq(s.velocity, 0.0f));
        assert(s.edge_hits == 1u);
    }

    audio_scratch_jog(&s, -2);
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(s.edge_latch == 0);
    assert(l == 900);
    assert(feq(audio_scratch_head_back(&s), 2.0f));
}

/* The fixed-window rate: velocity = ticks banked in the window * frames_per_tick
 * / window_samples, clamped. With a 1-sample window + instant slew each render
 * reads exactly the ticks banked since the previous render. */
static void test_rate_estimate_and_clamp(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 2.5f, HOLD_FOREVER);
    audio_scratch_seed(&s, 5.0f);

    int16_t l = 0, r = 0;
    /* Two ticks banked before this render -> velocity 2.0 (within the clamp). */
    audio_scratch_jog(&s, 1);
    audio_scratch_jog(&s, 1);
    (void)audio_scratch_render(&s, &b, &l, &r);
    assert(feq(s.velocity, 2.0f));

    /* 5 ticks -> 5.0 clamped to +2.5. */
    audio_scratch_jog(&s, 5);
    (void)audio_scratch_render(&s, &b, &l, &r);
    assert(feq(s.velocity, 2.5f));

    /* Hard reverse -> clamped to -2.5. */
    audio_scratch_jog(&s, -100);
    (void)audio_scratch_render(&s, &b, &l, &r);
    assert(feq(s.velocity, -2.5f));

    audio_scratch_seed(&s, 3.0f);        /* seeding zeroes the velocity */
    assert(feq(s.velocity, 0.0f));
}

/* Core Phase 5 property: between jog ticks the velocity HOLDS (steady platter
 * motion sounds continuous), and only after `hold_windows` tickless windows does
 * the platter stop (silence). The earlier estimator alternately froze the head
 * and spiked it to the window edge because it divided by a noisy render count. */
static void test_velocity_holds_between_ticks_then_stops(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    config_instant(&s, 1.0f, 100.0f, 3u);  /* stop after 3 empty windows */
    audio_scratch_seed(&s, 5.0f);
    audio_scratch_jog(&s, 1);

    int16_t l = 0, r = 0;
    /* Tick consumed; then tickless renders inside the hold window keep playing
     * at the held velocity. */
    assert(audio_scratch_render(&s, &b, &l, &r));   /* 400, head -> 4 */
    assert(l == 400);
    assert(audio_scratch_render(&s, &b, &l, &r));   /* empty win 1, hold -> 500 */
    assert(l == 500);
    assert(audio_scratch_render(&s, &b, &l, &r));   /* empty win 2, hold -> 600 */
    assert(l == 600);

    /* Third empty window: hand counted as stopped -> velocity slews to 0 -> silence. */
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(l == 0 && r == 0);
    assert(feq(s.velocity, 0.0f));
}

/* Realistic pattern: many small (+-1) ticks banked over a multi-sample window
 * average to a smooth mid velocity instead of spiking. */
static void test_windowed_average_of_dense_ticks(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    /* window = 4 samples, frames_per_tick = 2, instant slew, never stop. */
    audio_scratch_config(&s, 2.0f, 4u, 1.0f, 100.0f, HOLD_FOREVER);
    audio_scratch_seed(&s, 5.0f);

    /* 4 forward ticks banked across the 4-sample window. */
    for (int i = 0; i < 4; i++) {
        audio_scratch_jog(&s, 1);
    }

    int16_t l = 0, r = 0;
    /* First three renders are still inside the window (velocity_target unset =0)
     * so the platter reads as not yet moving. */
    (void)audio_scratch_render(&s, &b, &l, &r);
    (void)audio_scratch_render(&s, &b, &l, &r);
    (void)audio_scratch_render(&s, &b, &l, &r);
    assert(feq(s.velocity, 0.0f));
    /* On the 4th (window close): velocity = 4 ticks * 2 / 4 = 2.0. */
    (void)audio_scratch_render(&s, &b, &l, &r);
    assert(feq(s.velocity, 2.0f));
}

static void test_track_position_wraps_backward_inside_active_loop(void)
{
    /* Newest is 200 ms after loop start. Walking 500 ms backward must land
     * 300 ms before loop end, not before loop_start or at track zero. */
    uint32_t pos = audio_scratch_track_position_ms(
        10200u, 500.0f, 1000u, true, 10000u, 20000u);
    assert(pos == 19700u);

    /* Multiple complete loop lengths preserve the same modular position. */
    pos = audio_scratch_track_position_ms(
        10200u, 20500.0f, 1000u, true, 10000u, 20000u);
    assert(pos == 19700u);

    /* No/invalid loop retains the normal saturating linear mapping. */
    assert(audio_scratch_track_position_ms(
        1200u, 500.0f, 1000u, false, 0u, 0u) == 700u);
    assert(audio_scratch_track_position_ms(
        200u, 500.0f, 1000u, false, 0u, 0u) == 0u);
}

int main(void)
{
    test_inactive_and_stopped_are_silent();
    test_invalid_configuration_is_sanitized();
    test_forward_playback_ascends_toward_newest();
    test_reverse_playback_descends_toward_oldest();
    test_linear_interpolation();
    test_forward_past_newest_is_silent();
    test_reverse_past_oldest_is_silent();
    test_reversal_walks_head_back_into_window();
    test_window_edge_latches_silent_until_inward_reversal();
    test_rate_estimate_and_clamp();
    test_velocity_holds_between_ticks_then_stops();
    test_windowed_average_of_dense_ticks();
    test_track_position_wraps_backward_inside_active_loop();
    puts("audio_scratch tests passed");
    return 0;
}
