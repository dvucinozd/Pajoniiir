/* v274: loop X2 / /2 while the loop plays. The ring already holds the wrap
 * at the old end; the plan cuts it where the old and new loops part. */
#include "audio_loop_resize.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* 1 frame per ms keeps the model readable. */
#define SR 1000u
#define RING_MAX 4000u

typedef struct {
    uint32_t src[RING_MAX];       /* source ms of each frame ahead of the playhead */
    uint32_t used;
    uint32_t seek_base;
    uint64_t fs;
    bool since_wrap;
    uint32_t cursor;              /* next source ms the decoder writes */
} ring_t;

/* The decoder as the engine runs it: from the playhead, wrapping at end to
 * start, `ahead` frames written. The playhead may sit after a wrap. */
static void fill(ring_t *r, uint32_t play, uint32_t start, uint32_t end, uint32_t ahead,
                 uint32_t base, bool base_is_wrap, uint64_t played_since_base)
{
    r->used = 0;
    r->seek_base = base;
    r->since_wrap = base_is_wrap;
    r->fs = played_since_base;
    uint32_t t = play;
    for (uint32_t i = 0; i < ahead; i++) {
        if (t >= end) {
            t = start;
            r->seek_base = start;
            r->since_wrap = true;
            r->fs = 0;
        }
        r->src[r->used++] = t++;
        r->fs++;
    }
    r->cursor = t;
}

/* What the new loop plays from the playhead. */
static uint32_t expect_at(uint32_t play, uint32_t start, uint32_t end, uint32_t i)
{
    uint64_t t = (uint64_t)play + i;
    if (end != UINT32_MAX && t >= end) t = start + (t - end) % (end - start);
    return (uint32_t)t;
}

static audio_loop_resize_plan_t plan_for(const ring_t *r, uint32_t os, uint32_t oe,
                                         uint32_t ns, uint32_t ne)
{
    const audio_loop_resize_in_t in = {
        .old_start_ms = os, .old_end_ms = oe, .new_start_ms = ns, .new_end_ms = ne,
        .seek_base_ms = r->seek_base, .frames_since_seek = r->fs, .since_wrap = r->since_wrap,
        .ring_frames = r->used, .sample_rate = SR,
    };
    return audio_loop_resize_plan(&in);
}

/* After the plan, the kept frames and the decoder restart follow the new
 * loop; without a cut the ring and the decoder going on do, or the playhead
 * is past the new end (jump). */
static void check(uint32_t play, uint32_t os, uint32_t oe, uint32_t ns, uint32_t ne,
                  uint32_t ahead, uint32_t base, bool base_is_wrap, uint64_t since_base,
                  uint32_t *cuts, uint32_t *jumps)
{
    ring_t r;
    fill(&r, play, os, oe, ahead, base, base_is_wrap, since_base);
    const audio_loop_resize_plan_t p = plan_for(&r, os, oe, ns, ne);
    uint32_t jump;
    if (audio_loop_resize_jump_ms(play, ns, ne, &jump)) {
        assert(!p.cut || p.drop_frames < r.used);
        assert(jump >= ns && jump < ne);
        (*jumps)++;
        return;
    }
    if (!p.cut) {
        for (uint32_t i = 0; i < r.used; i++) assert(r.src[i] == expect_at(play, ns, ne, i));
        /* the decoder goes on from its cursor, wrapping at the new end */
        assert(r.cursor < ne || ne == UINT32_MAX || r.cursor == ne);
        return;
    }
    (*cuts)++;
    assert(p.drop_frames < r.used);
    const uint32_t keep = r.used - p.drop_frames;
    for (uint32_t i = 0; i < keep; i++) assert(r.src[i] == expect_at(play, ns, ne, i));
    assert(p.seek_ms == expect_at(play, ns, ne, keep));
    /* a continue lands mid-track at the old end, a wrap on the start */
    assert(p.exact ? p.seek_ms == oe : p.seek_ms == ns);
}

/* The hardware report: X2 on an active 2 s loop, the decoder ~2 s ahead and
 * already wrapped. */
static void test_double_while_wrapped(void)
{
    ring_t r;
    fill(&r, 11000u, 10000u, 12000u, 2000u, 10000u, true, 1000u);
    assert(r.since_wrap && r.fs == 1000u && r.used == 2000u);
    audio_loop_resize_plan_t p = plan_for(&r, 10000u, 12000u, 10000u, 14000u);
    /* v273 played the 1000 frames after the junction: back to 10000 at 12000 */
    assert(p.cut && p.exact && p.seek_ms == 12000u && p.drop_frames == 1000u);

    /* /2 at 10600: the ring cut at 11000, then the start */
    fill(&r, 10600u, 10000u, 12000u, 2000u, 10000u, true, 600u);
    p = plan_for(&r, 10000u, 12000u, 10000u, 11000u);
    assert(p.cut && !p.exact && p.seek_ms == 10000u && r.used - p.drop_frames == 400u);

    /* /2 at 11200: the new end is behind the playhead, a jump to 10200 */
    fill(&r, 11200u, 10000u, 12000u, 2000u, 10000u, true, 1200u);
    p = plan_for(&r, 10000u, 12000u, 10000u, 11000u);
    assert(!p.cut);
    uint32_t jump = 0;
    assert(audio_loop_resize_jump_ms(11200u, 10000u, 11000u, &jump) && jump == 10200u);
    assert(!audio_loop_resize_jump_ms(10999u, 10000u, 11000u, &jump));
}

/* A 1/8 beat loop (~60 ms) wraps ~33 times in a 2 s ring. */
static void test_short_loop_many_wraps(void)
{
    ring_t r;
    fill(&r, 10020u, 10000u, 10060u, 2000u, 10000u, true, 20u);
    audio_loop_resize_plan_t p = plan_for(&r, 10000u, 10060u, 10000u, 10120u);
    assert(p.cut && p.exact && p.seek_ms == 10060u && r.used - p.drop_frames == 40u);
    p = plan_for(&r, 10000u, 10060u, 10000u, 10030u);
    assert(p.cut && !p.exact && r.used - p.drop_frames == 10u);
}

/* Exit (reloop off) with the wrap already decoded: the track goes on. */
static void test_exit(void)
{
    ring_t r;
    fill(&r, 11500u, 10000u, 12000u, 2000u, 10000u, true, 1500u);
    audio_loop_resize_plan_t p = plan_for(&r, 10000u, 12000u, 10000u, UINT32_MAX);
    assert(p.cut && p.exact && p.seek_ms == 12000u && r.used - p.drop_frames == 500u);
    /* nothing changed: nothing to do */
    p = plan_for(&r, 10000u, 12000u, 10000u, 12000u);
    assert(!p.cut);
    audio_loop_resize_in_t bad = {.old_start_ms = 5u, .old_end_ms = 5u, .sample_rate = SR};
    assert(!audio_loop_resize_plan(&bad).cut && !audio_loop_resize_plan(NULL).cut);
}

/* Every playhead, fill and new length against the frame model. */
static void test_sweep(void)
{
    uint32_t cuts = 0, jumps = 0, cases = 0;
    const uint32_t os = 10000u;
    const uint32_t lens[] = {15u, 60u, 250u, 500u, 1000u, 2000u};
    for (size_t li = 0; li < sizeof lens / sizeof lens[0]; li++) {
        const uint32_t oe = os + lens[li];
        const uint32_t news[][2] = {
            {os, os + lens[li] * 2u}, {os, os + lens[li] / 2u}, {os, os + lens[li] * 4u},
            {os, os + lens[li] / 4u + 1u}, {os + lens[li] / 3u, oe}, {os, UINT32_MAX},
            {os, oe + 7u}, {os, oe - 7u},
        };
        for (uint32_t play = os; play < oe; play += 1u + lens[li] / 37u) {
            for (uint32_t ahead = 1u; ahead <= 2200u; ahead += 97u) {
                for (size_t ni = 0; ni < sizeof news / sizeof news[0]; ni++) {
                    const uint32_t ns = news[ni][0], ne = news[ni][1];
                    if (ne <= ns || ne <= os) continue;
                    /* playhead before or after the decoder's last wrap */
                    check(play, os, oe, ns, ne, ahead, os, true, play - os, &cuts, &jumps);
                    check(play, os, oe, ns, ne, ahead, play > 40u ? play - 40u : play, false,
                          40u, &cuts, &jumps);
                    cases += 2u;
                }
            }
        }
    }
    assert(cuts > 1000u && jumps > 100u);
    printf("  sweep: %u cases, %u cuts, %u jumps\n", cases, cuts, jumps);
}

int main(void)
{
    test_double_while_wrapped();
    test_short_loop_many_wraps();
    test_exit();
    test_sweep();
    printf("audio_loop_resize: all tests passed\n");
    return 0;
}
