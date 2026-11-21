"""Run production MT callback across consumer fallback after future PCM withdrawal.

Decoder/device timing is not simulated. The test proves source-clock ownership,
not a hardware deadline or the cause of a captured output stall.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
component = root / "firmware/p4-core/components/audio_engine"
source = (component / "audio_engine.c").read_text(encoding="utf-8")

def block(marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

code = r'''
#include "audio_keylock_selected.h"
#include "audio_resampler.h"
#include "audio_pcm_timeline.h"
#include <assert.h>
#include <stdio.h>
#define AE_DECK_0 0u
#define AUDIO_ENGINE_DECK_COUNT 2u
static int16_t pcm[192000*2];
static audio_pcm_timeline_t s_pcm_timelines[2];
static audio_keylock_selected_t s_keylocks[2];
static uint32_t s_keylock_generation[2];
static struct { bool timeline_preroll_pending; } s_engines[2];
static bool s_start_waiting[2], s_start_seek_pending[2];
static unsigned empty_reads, resets;
void __real_audio_keylock_reset(audio_keylock_selected_t *,uint64_t);
void __wrap_audio_keylock_reset(audio_keylock_selected_t *s,uint64_t seq) {
 ++resets;__real_audio_keylock_reset(s,seq);
}
static bool atomic_load_bool(const bool *p) { return *p; }
static bool timeline_active(uint8_t d) { return s_pcm_timelines[d].capacity > 0; }
#if !CONFIG_AUDIO_KEYLOCK_WSOLA2
static struct { bool keylock_dense_correlation, audio_antialias; } board = {false,true};
#define board_capabilities_get() (&board)
#endif
'''
code += block("static bool keylock_timeline_read(") + "\n"
code += block("static bool ae_keylock_render_cb(") + "\n"
code += r'''
static bool pop_frame(void *ctx,audio_mixer_frame_t *out) {
 uint8_t d=*(uint8_t *)ctx; bool ok=audio_pcm_timeline_pop(&s_pcm_timelines[d],out);
 if(!ok)++empty_reads;
 return ok;
}
int main(void) {
 unsigned cases=0, fallback=0, recovered=0,max_resets=0;
 for(uint8_t d=0;d<2;++d) for(unsigned r=0;r<2;++r) for(unsigned tempo=0;tempo<2;++tempo)
 for(unsigned phase=0;phase<256;phase+=85) for(unsigned mode=0;mode<7;++mode) {
  const unsigned keeps[]={0,1,32,256,2048,4800,94000};
  audio_pcm_timeline_t *t=&s_pcm_timelines[d];
  audio_pcm_timeline_init(t,pcm,192000);
  for(unsigned n=0;n<150000;++n) assert(audio_pcm_timeline_push(t,(n*97u)%24000,(n*71u)%24000));
  s_keylocks[d].initialized=false;
  float ratio=r?1.0f:44100.f/48000.f, speed=tempo?1.05f:.95f;
  audio_resampler_state_t rs={0};rs.antialias=true;
  for(unsigned n=0;n<8192+phase;++n) {
   audio_mixer_frame_t out;uint32_t consumed;
   assert(ae_keylock_render_cb(&d,speed,ratio,&out,&consumed));
  }
  unsigned future=audio_pcm_timeline_future_frames(t),keep=keeps[mode];
  if(keep>future)keep=future;
  audio_pcm_timeline_drop_newest(t,future-keep);
  uint32_t generation=audio_pcm_timeline_generation(t);
  unsigned resets_before=resets;
  bool fell_back=false;
  for(unsigned n=0;n<1024;++n) {
   audio_mixer_frame_t out;uint64_t before=audio_pcm_timeline_play_seq(t);uint32_t consumed=0;
   bool ok=ae_keylock_render_cb(&d,speed,ratio,&out,&consumed);
   if(!ok){++fallback;fell_back=true;out=audio_resampler_next(&rs,ratio*speed,pop_frame,&d,&consumed);}
   else if(fell_back){++recovered;fell_back=false;}
   uint64_t after=audio_pcm_timeline_play_seq(t);
   if(after<before) fprintf(stderr,"backward MT handoff deck=%u ratio=%f speed=%f phase=%u keep=%u frame=%u before=%llu after=%llu\n",d,ratio,speed,phase,keep,n,(unsigned long long)before,(unsigned long long)after);
   assert(after>=before);
   assert(after-before==consumed);
   assert(audio_pcm_timeline_generation(t)==generation); /* withdrawal is not a user seek */
   if(n==512) for(unsigned j=0;j<5000;++j) assert(audio_pcm_timeline_push(t,1200,-1200));
  }
  if(resets-resets_before>max_resets)max_resets=resets-resets_before;
  /* A real decoder/user seek resets the timeline generation and refills. */
  audio_pcm_timeline_reset(t);
  for(unsigned j=0;j<5000;++j) assert(audio_pcm_timeline_push(t,1200,-1200));
  audio_mixer_frame_t out;uint32_t consumed;
  uint64_t before=audio_pcm_timeline_play_seq(t);
  assert(ae_keylock_render_cb(&d,speed,ratio,&out,&consumed));
  assert(audio_pcm_timeline_play_seq(t)==before+consumed);
  ++cases;
 }
 assert(fallback && recovered && empty_reads);
 assert(max_resets<=4u); /* never turn a source gap into a full DSP reset per frame */
 printf("max_handoff_resets_per_1024_frames=%u\n",max_resets);
 puts("PASS production MT handoff: 224 same-generation future-withdrawal/fallback cases, both decks/rates/pitch signs; consumer never rewinds, actual seeks remain authoritative");
 return 0;
}
'''
build = Path(__file__).parent / "build/keylock-handoff"
build.mkdir(parents=True, exist_ok=True)
c = build / "handoff.c"
c.write_text(code, encoding="utf-8")
exe = build / ("handoff.exe" if os.name == "nt" else "handoff")
gcc = shutil.which("gcc")
if not gcc:
    raise RuntimeError("gcc required")
for version2 in (False, True):
    reset = "audio_keylock2_reset" if version2 else "audio_keylock_reset"
    algorithm = "audio_keylock_wsola2.c" if version2 else "audio_keylock.c"
    subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    f"-DCONFIG_AUDIO_KEYLOCK_WSOLA2={int(version2)}",
                    f"-DCONFIG_AUDIO_ANTIALIAS_CACHE={int(not version2)}",
                    f"-D__wrap_audio_keylock_reset=__wrap_{reset}",
                    f"-D__real_audio_keylock_reset=__real_{reset}",
                    "-I" + str(component / "include"), str(c), str(component / algorithm),
                    *([str(component / "wsola2_resampler.c")] if version2 else []),
                    str(component / "audio_pcm_timeline.c"), str(component / "audio_resampler.c"),
                    f"-Wl,--wrap={reset}", "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
