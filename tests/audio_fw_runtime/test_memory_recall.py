"""Exercise firmware recall publication at decoder lock boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "firmware/p4-core/components/audio_engine/audio_engine.c").read_text(encoding="utf-8")
signature = "static esp_err_t audio_engine_seek_with_optional_loop("
seek = source.split(signature, 1)[1].split("static esp_err_t audio_engine_seek_for_deck_reason(", 1)[0]
decode = source.split("const size_t predicted_read_offset = ae_next_read_offset(eng, fw);", 1)[1]
decode = decode.split("ae_warm_cache_for_next_read(eng, fw);", 1)[1]
decode = decode.split("        const size_t backend_before", 1)[0]
prelude = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define AE_FW 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define AE_SEEK_PREBUFFER_FRAMES 512u
typedef int esp_err_t;
typedef enum { AE_SEEK_REASON_USER, AE_SEEK_REASON_LOOP } ae_seek_reason_t;
typedef struct {
    bool loaded, playing, paused, loop_active, loop_resize_pending, seek_requested;
    bool eof, playback_finished, usb_resume_pending, decoder_open;
    uint32_t duration_ms, loop_start_ms, loop_end_ms, seek_target_ms;
    uint32_t seek_base_ms, output_base_ms, output_frames_since_seek, frames_since_seek;
    uint32_t usb_resume_position_ms, mp3_decode_length, sample_rate, decoder;
    uint8_t seek_reason;
} audio_engine_state_t;
static audio_engine_state_t s_engines[1];
static struct { bool source; } s_fw_preloads[1] = {{true}};
static uint32_t s_start_prebuffer_frames[1], s_start_wait_count[1];
static bool s_start_seek_pending[1], s_start_waiting[1];
static unsigned locks, publications;
static bool observe_recall;
static uint32_t expected_start, expected_end;
static bool atomic_load_bool(const bool *flag) { return *flag; }
static void atomic_store_bool(bool *flag, bool value) { *flag = value; }
static void atomic_store_u32(uint32_t *field, uint32_t value) { *field = value; }
static void audio_track_decode_length_invalidate(uint32_t *value) { *value = 0; }
static int audio_decoder_seek_frame(uint32_t *decoder, uint64_t frame)
{ (void)decoder; (void)frame; assert(false); return ESP_ERR_INVALID_STATE; }
static void output_position_epoch_bump(uint8_t deck) { assert(deck == 0 && locks == 1); }
static bool audio_loop_range_valid(uint32_t start, uint32_t end, uint32_t duration)
{ return start < end && end <= duration; }
static void AE_LOCK(void) { assert(locks == 0); ++locks; }
static void AE_UNLOCK(void)
{
    assert(locks == 1);
    --locks;
    const audio_engine_state_t *eng = &s_engines[0];
    if (!observe_recall || !eng->seek_requested) return;
    /* Observe the decoder-visible state at the first unlock after publication. */
    ++publications;
    assert(eng->seek_reason == AE_SEEK_REASON_USER && eng->seek_target_ms == expected_start);
    assert(eng->loop_active == (expected_end != 0));
    assert(eng->loop_start_ms == (expected_end ? expected_start : 0));
    assert(eng->loop_end_ms == expected_end && !eng->loop_resize_pending);
    assert(s_start_seek_pending[0] && s_start_prebuffer_frames[0] == AE_SEEK_PREBUFFER_FRAMES);
}
"""
decoder_test = r"""
static unsigned decode_attempts(void)
{
    audio_engine_state_t *eng = &s_engines[0];
    unsigned attempts = 0;
    for (unsigned i = 0; i < 1; ++i) {
""" + decode + r"""
        ++attempts;
        AE_UNLOCK();
    }
    return attempts;
}
"""
cases = r"""
int main(void)
{
    audio_engine_state_t *eng = &s_engines[0];
    *eng = (audio_engine_state_t){.loaded=true, .playing=true, .duration_ms=10000,
        .loop_active=true, .loop_resize_pending=true, .loop_start_ms=1000, .loop_end_ms=2000};
    uint32_t end = 6000;
    assert(audio_engine_seek_with_optional_loop(0, 5000, AE_SEEK_REASON_USER, &end) == ESP_ERR_INVALID_STATE);
    assert(!eng->seek_requested && eng->loop_active && eng->loop_start_ms == 1000);
    eng->playing = false;
    expected_start = 5000;
    expected_end = end;
    observe_recall = true;
    assert(audio_engine_seek_with_optional_loop(0, 5000, AE_SEEK_REASON_USER, &end) == ESP_OK);
    observe_recall = false;
    assert(publications == 1 && decode_attempts() == 0);
    eng->seek_requested = false;
    assert(decode_attempts() == 1);
    end = 0;
    expected_start = 3000;
    expected_end = 0;
    observe_recall = true;
    assert(audio_engine_seek_with_optional_loop(0, 3000, AE_SEEK_REASON_USER, &end) == ESP_OK);
    assert(publications == 2);
}
"""
with tempfile.TemporaryDirectory(prefix="p4-memory-recall-") as temporary:
    folder = Path(temporary)
    test = folder / "test.c"
    executable = folder / "test.exe"
    test.write_text(prelude + "\n" + signature + seek + decoder_test + cases, encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-Wall", "-Wextra",
                    "-Werror", str(test), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS firmware recall publishes seek and loop together and defers a stale decode pass")
