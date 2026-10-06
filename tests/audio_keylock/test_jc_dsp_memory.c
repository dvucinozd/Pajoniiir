#include "audio_keylock.h"
#include "audio_resampler.h"
#include <assert.h>
#include <stdio.h>
_Static_assert(!AUDIO_ANTIALIAS_CACHE, "JC regression must compile without FIR storage");
_Static_assert(sizeof(audio_keylock_t) <= 640 * 5 + 128, "JC keylock grew beyond cache/control budget");
_Static_assert(sizeof(audio_resampler_state_t) <= 40, "JC resampler must not reserve FIR history");
static bool read_constant(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{ (void)ctx; (void)seq; *out=(audio_mixer_frame_t){2000,-2000}; return true; }
static bool pop_constant(void *ctx, audio_mixer_frame_t *out)
{ return read_constant(ctx,0,out); }
int main(void)
{
    audio_keylock_t keylock;
    audio_resampler_state_t resampler;
    audio_keylock_reset(&keylock,0); audio_resampler_reset(&resampler);
    for(unsigned i=0;i<1024;++i) {
        audio_mixer_frame_t out; uint32_t consumed; uint64_t position;
        assert(audio_keylock_next(&keylock,read_constant,NULL,&out,&consumed,&position));
        assert(out.left==2000 && out.right==-2000 && consumed==1 && position==i+1);
        out=audio_resampler_next(&resampler,1,pop_constant,NULL,&consumed);
        assert(consumed==1);
        if(i) assert(out.left==2000 && out.right==-2000);
    }
    printf("PASS JC production DSP memory: keylock=%zu resampler=%zu, steady PCM and clock preserved\n",
        sizeof keylock,sizeof resampler);
    return 0;
}
