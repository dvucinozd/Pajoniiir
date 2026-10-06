"""Execute the actual production session-fenced network mutation with locks.
This is ownership coverage, not a scheduler, audio phase or hardware test.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'firmware/p4-core/components/audio_engine/audio_engine.c').read_text(encoding='utf-8')
start = source.index('esp_err_t audio_engine_deck_apply_network(')
opening = source.index('{', start)
end, depth = opening + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#define AE_FW 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define pdTRUE 1
typedef int esp_err_t;
static int tokens[2]={1,1};
static int *s_lifecycle_mutex[2]={&tokens[0],&tokens[1]};
static uint32_t s_lifecycle_session_generation[2]={10,20};
static struct {bool loaded;uint32_t loaded_session_generation;} s_engines[2]={{true,10},{true,20}};
static bool s_scratch_playing[2],s_deck_hold[2];
static unsigned seeks,pitches;
static bool deck_is_valid(uint8_t d) {return d<2;}
static bool atomic_load_bool(const bool *p) {return *p;}
static int xSemaphoreTake(int *token,unsigned timeout) {
    assert(!timeout);if (*token) {--*token;return pdTRUE;}return 0;
}
static void lifecycle_deck_unlock(uint8_t d) {assert(!tokens[d]);++tokens[d];}
static int audio_engine_request_user_seek(uint8_t d,uint32_t p) {
    assert(!tokens[d] && p==1234);++seeks;return ESP_OK;
}
static void audio_engine_set_pitch_percent_for_deck(uint8_t d,float p) {
    assert(!tokens[d] && p==2.4f);++pitches;
}
'''
code += source[start:end]
code += r'''
int main(void) {
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,2.4f)==ESP_OK);
    assert(seeks==1 && pitches==1 && tokens[0]==1);
    assert(audio_engine_deck_apply_network(0,9,true,1234,true,2.4f)==ESP_ERR_INVALID_STATE);
    s_engines[0].loaded_session_generation=11;
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,2.4f)==ESP_ERR_INVALID_STATE);
    s_engines[0].loaded_session_generation=10;tokens[0]=0;
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,2.4f)==ESP_ERR_TIMEOUT);
    tokens[0]=1;s_scratch_playing[0]=true;
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,2.4f)==ESP_ERR_INVALID_STATE);
    s_scratch_playing[0]=false;s_deck_hold[0]=true;
    assert(audio_engine_deck_apply_network(0,10,false,0,true,2.4f)==ESP_ERR_INVALID_STATE);
    s_deck_hold[0]=false;s_engines[0].loaded=false;
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,2.4f)==ESP_ERR_INVALID_STATE);
    assert(audio_engine_deck_apply_network(2,10,true,1234,true,2.4f)==ESP_ERR_INVALID_ARG);
    assert(audio_engine_deck_apply_network(0,0,true,1234,true,2.4f)==ESP_ERR_INVALID_ARG);
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,NAN)==ESP_ERR_INVALID_ARG);
    assert(audio_engine_deck_apply_network(0,10,true,1234,true,21)==ESP_ERR_INVALID_ARG);
    assert(seeks==1 && pitches==1 && tokens[0]==1);
    puts("PASS production network mutation: session replacement, nonblocking lock, scratch, hold, invalid pitch");
    return 0;
}
'''
out = root / '.cache/network-lifecycle'
out.mkdir(parents=True, exist_ok=True)
c_file = out / 'test.c'
c_file.write_text(code, encoding='utf-8')
target = out / ('test.exe' if os.name == 'nt' else 'test')
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                *flags, str(c_file), '-lm', '-o', str(target)], check=True)
subprocess.run([str(target)], check=True, timeout=30)
