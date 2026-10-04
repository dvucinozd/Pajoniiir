"""Execute production firmware teardown and EOF boundaries with RTOS stubs.

Extract the actual static functions/abort branches, not copied implementations.
No device, scheduler timing or storage latency is simulated by this test.
"""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "firmware/main-deck-p4/components/audio_engine"
BUILD = Path(__file__).resolve().parent / "build/lifecycle"
BUILD.mkdir(parents=True, exist_ok=True)
source = (COMPONENT / "audio_engine.c").read_text(encoding="utf-8")


def block(marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# A CUE/restart can leave both deck renderers temporarily inactive while the
# FLX4 isochronous endpoint continues consuming frames.  The production idle
# branch must therefore submit a zero block to UAC and pace it with the main
# I2S sink instead of sleeping and allowing the UAC ring to drain.
idle_output_block = block("if (!deck0.active && !deck1.active) {")
for required in (
    "memset(master_out, 0",
    "memset(hp_out, 0",
    "controller_usb_host_write_audio(",
    "audio_output_write_main(",
):
    if required not in idle_output_block:
        raise AssertionError(
            f"firmware idle output does not preserve UAC continuity: {required}"
        )


prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_fw_runtime.h"
#include "audio_eof_policy.h"
#define AE_FW 1
#define AUDIO_ENGINE_DECK_COUNT 2
#define AE_START_PREBUFFER_FRAMES 512u
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static bool lock_available = true;
static unsigned lock_depth;
#define AE_LOCK() do { assert(lock_available); ++lock_depth; } while (0)
#define AE_TRY_LOCK() (lock_available ? (++lock_depth, true) : false)
#define AE_UNLOCK() do { assert(lock_depth); --lock_depth; } while (0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef int *SemaphoreHandle_t;
typedef struct {
    bool loaded, playing, paused, playback_finished, loading, eof, decoder_open;
    bool timeline_preroll_pending;
    uint32_t timeline_preroll_frames;
    bool flac_ready, flac_recovery_pending;
    unsigned load_progress;
    uint64_t flac_resume_frame;
    FILE *fp;
    void *flac;
    int decoder, last_error;
    char last_error_text[32];
    size_t file_size, file_pos;
} audio_engine_state_t;
typedef struct { void *source; void *buf; char path[272]; } audio_fw_preload_t;
typedef int drflac;
static audio_engine_state_t s_engines[2];
static audio_fw_runtime_t s_fw_runtimes[2];
static audio_fw_preload_t s_fw_preloads[2];
static bool s_start_waiting[2], s_start_seek_pending[2];
static uint32_t s_start_prebuffer_frames[2];
static bool s_deck_hold[2], s_scratch_playing[2], s_scratch_abort_seek_waiting[2];
static int s_fw_task_contexts[2], s_scratch_buf[2], s_resamplers[2];
static struct { bool initialized; } s_keylocks[2];
static int tokens, freed, reset;
static void *s_codec;
static SemaphoreHandle_t s_tasks_done[2] = { &tokens, NULL };
static int xSemaphoreTake(SemaphoreHandle_t sem, unsigned timeout) {
    (void)timeout;
    if (*sem > 0) { --*sem; return pdTRUE; }
    return 0;
}
static void atomic_store_bool(bool *dst, bool value) { *dst = value; }
static bool atomic_load_bool(const bool *src) { return *src; }
static void atomic_store_u32(uint32_t *dst, uint32_t value) { *dst = value; }
static void audio_fw_task_context_reset(int *ctx) { *ctx = 0; }
static void audio_decoder_close(int *ctx) { (void)ctx; }
static void drflac_close(drflac *ctx) { (void)ctx; }
static void esp_codec_dev_close(void *ctx) { (void)ctx; }
static unsigned usb_gate, sd_gate;
static void media_io_gate_begin(void) {++usb_gate;}
static void media_io_gate_end(void) {assert(usb_gate);--usb_gate;}
static bool sd_gate_available = true;
static bool sd_io_gate_try_begin(unsigned ms) {assert(ms==50);if(!sd_gate_available)return false;++sd_gate;return true;}
static void sd_io_gate_begin(void) {++sd_gate;}
static void sd_io_gate_end(void) {assert(sd_gate);--sd_gate;}
static void heap_caps_free(void *p) { ++freed; free(p); }
static void audio_fw_preload_begin_load(audio_fw_preload_t *fw) {
    memset(fw, 0, sizeof(*fw));
}
static void ae_clear_read_faults(uint8_t deck) { (void)deck; }
static void audio_engine_reset_state(audio_engine_state_t *eng, int rc,
                                     const char *message) {
    (void)rc; (void)message; ++reset; memset(eng, 0, sizeof(*eng));
}
static void output_position_epoch_bump(uint8_t deck) { (void)deck; }
static void deck_pcm_reset(uint8_t deck) { (void)deck; }
static unsigned deck_pcm_used(uint8_t deck) { (void)deck; return 0; }
static void audio_resampler_reset(int *resampler) { *resampler = 0; }
static void audio_scratch_buffer_reset(int *buf) { *buf = 0; }
static unsigned output_stops, advanced[2], lifecycle_depth;
static bool s_lifecycle_loads_blocked;
static void lifecycle_admission_lock(void) {}
static void lifecycle_admission_unlock(void) {}
static void lifecycle_deck_lock(uint8_t d) {(void)d;++lifecycle_depth;}
static void lifecycle_deck_unlock(uint8_t d) {(void)d;assert(lifecycle_depth);--lifecycle_depth;}
static uint32_t lifecycle_advance_generation(uint8_t d) {return ++advanced[d];}
static esp_err_t audio_output_service_stop(void) {++output_stops;return ESP_OK;}
static bool s_scratch_abort_seek_requested[2], s_scratch_capture_freeze[2];
static uint32_t s_scratch_abort_seek_target_ms[2];
static int s_scratch_engine[2], s_scratch_handoff[2];
static float s_scratch_handoff_gain[2];
static unsigned s_scratch_handoff_applied[2], s_scratch_handoff_command[2];
static unsigned seek_calls, last_target;
#define AE_SCRATCH_HANDOFF_NONE 0
#define AE_SEEK_REASON_SCRATCH_ABORT 3
static void audio_scratch_end(int *engine) { *engine = 0; }
static void scratch_handoff_store(int *handoff, int value) { *handoff = value; }
static void apply_pending_pitch(unsigned deck) { (void)deck; }
static int audio_engine_seek_for_deck_reason(unsigned deck, unsigned target, int reason) {
    (void)deck;
    assert(lock_depth > 0 && reason == AE_SEEK_REASON_SCRATCH_ABORT);
    AE_LOCK(); /* The production seek publisher takes the recursive mutex. */
    ++seek_calls;
    last_target = target;
    AE_UNLOCK();
    return ESP_OK;
}
'''

abort_wrapper = r'''
static int abort_load(int output_rc, int expected_tasks) {
    unsigned deck = 0;
    audio_engine_state_t *eng = &s_engines[deck];
    audio_fw_runtime_t *runtime = &s_fw_runtimes[deck];
    audio_fw_preload_t *fw = &s_fw_preloads[deck];
    int *task_ctx = &s_fw_task_contexts[deck];
    struct { int expected_tasks; } task_plan = { expected_tasks };
'''

main = r'''
static void owned_session(void) {
    audio_fw_runtime_begin_load(&s_fw_runtimes[0]);
    s_fw_runtimes[0].tasks_started = 2;
    s_fw_runtimes[0].loader_task = (void *)1;
    s_fw_runtimes[0].decode_task = (void *)2;
    s_fw_task_contexts[0] = 123;
    s_fw_preloads[0].buf = malloc(32);
    assert(s_fw_preloads[0].buf);
    freed = reset = 0;
}
static void assert_retained(void) {
    assert(s_fw_runtimes[0].tasks_started == 1);
    assert(s_fw_runtimes[0].loader_task && s_fw_runtimes[0].decode_task);
    assert(s_fw_task_contexts[0] == 123);
    assert(s_fw_preloads[0].buf && !freed && !reset);
}
static void finish_stop(void) {
    tokens = 0;
    assert(audio_engine_stop_for_deck(0) == ESP_ERR_TIMEOUT);
    assert_retained();
    tokens = 1;
    assert(audio_engine_stop_for_deck(0) == ESP_OK);
    assert(s_fw_runtimes[0].tasks_started == 0);
    assert(s_fw_task_contexts[0] == 0);
    assert(!s_fw_preloads[0].buf && freed == 1 && reset == 1);
    assert(audio_engine_stop_for_deck(0) == ESP_OK);
}
int main(void) {
    audio_fw_preload_t source_file={0};
    strcpy(source_file.path,"/sd/djlcache/track.MP3");
    assert(ae_source_gate_begin(&source_file) && sd_gate==1 && !usb_gate);
    ae_source_gate_end(&source_file);
    sd_gate_available=false;assert(!ae_source_gate_begin(&source_file) && !sd_gate);sd_gate_available=true;
    source_file.source=tmpfile();assert(source_file.source);
    ae_source_close(&source_file,(FILE *)source_file.source);assert(!sd_gate && !usb_gate);
    strcpy(source_file.path,"/usb/track.MP3");
    assert(ae_source_gate_begin(&source_file) && usb_gate==1 && !sd_gate);ae_source_gate_end(&source_file);
    strcpy(s_fw_preloads[0].path,"/sd/cache/track.MP3");
    strcpy(s_fw_preloads[1].path,"/usb/track.MP3");
    s_engines[0].loaded=s_engines[0].playing=s_engines[1].loaded=true;
    bool acquired=false;
    assert(suspend_loads_and_stop_selected(&acquired,true)==ESP_OK && acquired);
    assert(s_engines[0].loaded && s_engines[0].playing && !s_engines[1].loaded);
    assert(!advanced[0] && advanced[1]==1 && !output_stops && !lifecycle_depth);
    s_lifecycle_loads_blocked=false;s_engines[0].loaded=false;s_engines[0].loading=true;
    assert(suspend_loads_and_stop_selected(&acquired,true)==ESP_OK);
    assert(s_engines[0].loading && !advanced[0] && !output_stops);
    s_lifecycle_loads_blocked=false;s_engines[0].loading=false;s_engines[0].playing=false;
    owned_session();
    tokens = 1;
    assert(audio_engine_stop_for_deck(0) == ESP_ERR_TIMEOUT);
    assert_retained();
    finish_stop();
    for (unsigned failure = 0; failure < 2; ++failure) {
        owned_session();
        tokens = 1;
        int rc = abort_load(failure ? ESP_OK : ESP_ERR_NO_MEM, 3);
        assert(rc == ESP_ERR_NO_MEM);
        assert_retained();
        finish_stop();
        owned_session();
        tokens = 2;
        assert(abort_load(failure ? ESP_OK : ESP_ERR_NO_MEM, 3) == ESP_ERR_NO_MEM);
        assert(s_fw_runtimes[0].tasks_started == 0);
        assert(s_fw_task_contexts[0] == 0 && freed == 1 && reset == 1);
    }
    s_engines[0].eof = s_engines[0].playing = true;
    lock_available = false;
    complete_eof_drain_if_ready(0);
    assert(s_engines[0].playing && !s_engines[0].playback_finished);
    lock_available = true;
    complete_eof_drain_if_ready(0);
    assert(!s_engines[0].playing && s_engines[0].playback_finished);
    s_scratch_abort_seek_requested[0] = true;
    s_scratch_abort_seek_target_ms[0] = 12000;
    s_scratch_playing[0] = s_scratch_capture_freeze[0] = true;
    lock_available = false;
    output_scratch_abort();
    assert(s_scratch_abort_seek_requested[0] && s_scratch_playing[0] && !seek_calls);
    lock_available = true;
    output_scratch_abort();
    assert(!s_scratch_abort_seek_requested[0] && !s_scratch_playing[0]);
    assert(!s_scratch_capture_freeze[0] && s_scratch_abort_seek_waiting[0]);
    assert(seek_calls == 1 && last_target == 12000);
    assert(lock_depth == 0);
    puts("PASS production STOP retries, both startup aborts retain worker ownership, deferred EOF/scratch seek");
    return 0;
}
'''

code = prelude + block("static bool ae_source_sd(const audio_fw_preload_t *fw)\n")
code += block("static bool ae_source_gate_begin(audio_fw_preload_t *fw)\n")
code += block("static void ae_source_gate_end(audio_fw_preload_t *fw)\n")
code += block("static void ae_source_close(audio_fw_preload_t *fw,FILE *fp)\n")
code += block("static bool audio_wait_worker_exit(void *ctx)\n")
code += block("static bool any_deck_loaded(void)\n")
code += block("static esp_err_t audio_engine_stop_for_deck(uint8_t deck)\n{")
code += block("static esp_err_t suspend_loads_and_stop_selected(bool *out_acquired,bool usb_only)\n")
code += block("static void complete_eof_drain_if_ready(uint8_t deck)\n")
code += "\nstatic void output_scratch_abort(void) { for (unsigned d = 0; d < 2; ++d) {\n"
code += block("if (atomic_load_bool(&s_scratch_abort_seek_requested[d]))")
code += "\n}}\n"
# The two branches below live inside LOAD, after task creation.
abort_start = source.index("esp_err_t output_rc = audio_output_service_ensure_started();")
source = source[abort_start:]
code += abort_wrapper + block("if (output_rc != ESP_OK)")
code += block("if (runtime->tasks_started != task_plan.expected_tasks)")
code += "\nreturn ESP_OK;\n}\n" + main
target = BUILD / ("test_lifecycle.exe" if os.name == "nt" else "test_lifecycle")
c_file = BUILD / "test_lifecycle.c"
c_file.write_text(code, encoding="utf-8")
gcc = shutil.which("gcc")
if not gcc:
    raise RuntimeError("gcc is required on PATH")
subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I" + str(COMPONENT / "include"), str(c_file),
                str(COMPONENT / "audio_fw_runtime.c"),
                str(COMPONENT / "audio_eof_policy.c"), "-o", str(target)], check=True)
subprocess.run([str(target)], check=True, timeout=30)
