#include "audio_loop_prefix.h"
#include "audio_loop_resize.h"
#include <assert.h>
#include <stdio.h>

static int16_t source[120000 * 2], prefix_pcm[96000 * 2];
static audio_pcm_timeline_t timeline;

static audio_loop_prefix_t captured(uint32_t rate)
{
    audio_pcm_timeline_init(&timeline, source, 120000);
    for (uint32_t i = 0; i < 110000; ++i)
        assert(audio_pcm_timeline_push(&timeline, (int16_t)(i % 30000),
                                       (int16_t)-(int32_t)(i % 30000)));
    audio_loop_prefix_t p = {.pcm=prefix_pcm, .capacity=96000};
    assert(audio_loop_prefix_capture(&p, &timeline, 1000, 2000, rate, 42));
    return p;
}

static void test_resume_and_short_laps(void)
{
    const uint32_t rates[] = {44100,48000,96000};
    for (unsigned r = 0; r < 3; ++r) {
        audio_loop_prefix_t p = captured(rates[r]);
        audio_loop_prefix_plan_t plan = audio_loop_prefix_plan(&p,42,2000,5000);
        assert(plan.frames == rates[r] && plan.seek_ms == 3000);
        for(uint32_t i=0;i<plan.frames;++i) {
            audio_mixer_frame_t f;
            assert(audio_loop_prefix_frame(&p,&plan,i,&f));
            assert(f.left == (int16_t)((1000+i)%30000));
            assert(f.right == -f.left);
        }
        plan=audio_loop_prefix_plan(&p,42,2000,2010);
        assert(plan.frames <= p.frames && plan.frames >= rates[r]/2);
        assert(plan.seek_ms == 2000 && plan.frames%plan.period_frames == 0);
        audio_mixer_frame_t f;
        assert(audio_loop_prefix_frame(&p,&plan,plan.period_frames,&f));
        assert(f.left == 1000); /* exact first sample of the next complete lap */
    }
}

static void test_stale_and_missing_history(void)
{
    audio_loop_prefix_t p=captured(44100);
    assert(!audio_loop_prefix_plan(&p,43,2000,5000).frames);
    assert(!audio_loop_prefix_plan(&p,42,2001,5000).frames);
    assert(!audio_loop_prefix_plan(&p,42,2000,2000).frames);
    assert(!audio_loop_prefix_capture(&p,&timeline,109999,0,44100,42));
    assert(!p.valid); /* partial capture cannot reuse the old song */
    assert(!audio_loop_prefix_capture(&p,&timeline,110000,0,44100,42));
    assert(!audio_loop_prefix_capture(&p,&timeline,0,0,0,42));
    p.pcm=NULL;
    assert(!audio_loop_prefix_capture(&p,&timeline,1000,0,44100,42));
}

static void test_manual_out_slow_seek_runway(void)
{
    audio_loop_prefix_t p=captured(44100);
    audio_loop_prefix_plan_t plan=audio_loop_prefix_plan(&p,42,2000,5000);
    audio_pcm_timeline_reset(&timeline);
    for(unsigned i=0;i<2048;++i) assert(audio_pcm_timeline_push(&timeline,7,7));
    audio_mixer_frame_t f;
    for(unsigned i=0;i<2048;++i) assert(audio_pcm_timeline_pop(&timeline,&f));
    assert(!audio_pcm_timeline_pop(&timeline,&f)); /* old reserve is <100ms */
    audio_pcm_timeline_reset(&timeline);
    for(unsigned i=0;i<2048;++i) assert(audio_pcm_timeline_push(&timeline,7,7));
    for(uint32_t i=0;i<plan.frames;++i) {
        assert(audio_loop_prefix_frame(&p,&plan,i,&f));
        assert(audio_pcm_timeline_push(&timeline,f.left,f.right));
    }
    for(unsigned i=0;i<4410;++i) assert(audio_pcm_timeline_pop(&timeline,&f));
    assert(audio_pcm_timeline_future_frames(&timeline)>40000);
    assert(plan.seek_ms == 3000); /* seek resumes after copied source, no skip */
}

static void test_resize_after_prefix_continuation(void)
{
    audio_loop_prefix_t p=captured(44100);
    audio_loop_prefix_plan_t prefix=audio_loop_prefix_plan(&p,42,2000,5000);
    assert(prefix.seek_ms==3000); /* mid-loop continuation, not wrap to IN */
    audio_loop_resize_in_t in={.old_start_ms=2000,.old_end_ms=5000,
        .new_start_ms=2000,.new_end_ms=3400,.seek_base_ms=prefix.seek_ms,
        .frames_since_seek=22050,.ring_frames=66150,.sample_rate=44100,
        .since_wrap=false};
    audio_loop_resize_plan_t plan=audio_loop_resize_plan(&in);
    assert(plan.cut && plan.drop_frames==4410 && plan.seek_ms==2000);
    /* The wrong wrap label misinterprets the copied prefix as complete laps. */
    in.since_wrap=true;
    assert(!audio_loop_resize_plan(&in).cut);
}

int main(void)
{
    test_resume_and_short_laps();
    test_stale_and_missing_history();
    test_manual_out_slow_seek_runway();
    test_resize_after_prefix_continuation();
    puts("PASS loop prefix: slow seek runway, source continuity, short laps, stale/missing capture");
    return 0;
}
