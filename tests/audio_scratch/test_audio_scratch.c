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

    /* Seeded but velocity 0 (platter held still) -> silence. */
    audio_scratch_seed(&s, 4.0f);
    assert(audio_scratch_is_active(&s));
    l = 7; r = 7;
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(l == 0 && r == 0);
}

static void test_forward_playback_ascends_toward_newest(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    audio_scratch_config(&s, 1.0f, 1.0f, 100.0f);  /* no decay for clean steps */
    audio_scratch_seed(&s, 5.0f);
    audio_scratch_jog(&s, 1);   /* velocity = +1 (forward, toward newer) */

    int16_t l = 0, r = 0;
    assert(audio_scratch_render(&s, &b, &l, &r));
    assert(l == 400 && r == -400);   /* back 5 -> (9-5)*100 */
    assert(audio_scratch_render(&s, &b, &l, &r));
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
    audio_scratch_config(&s, 1.0f, 1.0f, 100.0f);
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
    audio_scratch_config(&s, 0.5f, 1.0f, 100.0f);
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
    audio_scratch_config(&s, 1.0f, 1.0f, 100.0f);
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
    audio_scratch_config(&s, 1.0f, 1.0f, 100.0f);
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
    audio_scratch_config(&s, 1.0f, 1.0f, 100.0f);
    audio_scratch_seed(&s, 0.0f);
    audio_scratch_jog(&s, 1);   /* forward past newest -> silent, head pinned 0 */

    int16_t l = 0, r = 0;
    assert(!audio_scratch_render(&s, &b, &l, &r));
    assert(feq(audio_scratch_head_back(&s), 0.0f));

    /* Reverse hard: two ticks back -> velocity -1, head walks into the window. */
    audio_scratch_jog(&s, -2);
    assert(audio_scratch_render(&s, &b, &l, &r));  /* now audible again */
    assert(l == 900);                              /* back 0 = newest */
    assert(feq(audio_scratch_head_back(&s), 1.0f));
}

static void test_jog_accumulates_and_clamps_velocity(void)
{
    audio_scratch_t s;
    audio_scratch_init(&s);
    audio_scratch_config(&s, 1.0f, 1.0f, 2.5f);

    audio_scratch_jog(&s, 1);
    assert(feq(s.velocity, 1.0f));
    audio_scratch_jog(&s, 1);
    assert(feq(s.velocity, 2.0f));
    audio_scratch_jog(&s, 5);            /* would be 7.0, clamped to +2.5 */
    assert(feq(s.velocity, 2.5f));
    audio_scratch_jog(&s, -100);         /* clamped to -2.5 */
    assert(feq(s.velocity, -2.5f));

    audio_scratch_seed(&s, 3.0f);        /* seeding zeroes the velocity */
    assert(feq(s.velocity, 0.0f));
}

static void test_velocity_decays_toward_stop(void)
{
    audio_scratch_buffer_t b;
    fill_window(&b);

    audio_scratch_t s;
    audio_scratch_init(&s);
    audio_scratch_config(&s, 1.0f, 0.5f, 100.0f);  /* fast decay */
    audio_scratch_seed(&s, 5.0f);
    audio_scratch_jog(&s, 2);   /* velocity = 2.0 */

    int16_t l = 0, r = 0;
    (void)audio_scratch_render(&s, &b, &l, &r);   /* velocity 2.0 -> 1.0 */
    assert(feq(s.velocity, 1.0f));
    (void)audio_scratch_render(&s, &b, &l, &r);   /* 1.0 -> 0.5 */
    assert(feq(s.velocity, 0.5f));
}

int main(void)
{
    test_inactive_and_stopped_are_silent();
    test_forward_playback_ascends_toward_newest();
    test_reverse_playback_descends_toward_oldest();
    test_linear_interpolation();
    test_forward_past_newest_is_silent();
    test_reverse_past_oldest_is_silent();
    test_reversal_walks_head_back_into_window();
    test_jog_accumulates_and_clamps_velocity();
    test_velocity_decays_toward_stop();
    puts("audio_scratch tests passed");
    return 0;
}
