#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

candidates = {
    "audio_scratch_buffer_push": {
        "firmware/main-deck-p4/components/audio_engine/include/audio_scratch_buffer.h",
        "firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c",
        "tests/audio_scratch_buffer/test_audio_scratch_buffer.c",
    },
    "audio_scratch_buffer_index_for_ms": {
        "firmware/main-deck-p4/components/audio_engine/include/audio_scratch_buffer.h",
        "firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c",
        "tests/audio_scratch_buffer/test_audio_scratch_buffer.c",
    },
    "audio_scratch_buffer_read": {
        "firmware/main-deck-p4/components/audio_engine/include/audio_scratch_buffer.h",
        "firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c",
        "tests/audio_scratch_buffer/test_audio_scratch_buffer.c",
    },
    "audio_output_block_period_ms": {
        "firmware/main-deck-p4/components/audio_engine/include/audio_output_timing.h",
        "firmware/main-deck-p4/components/audio_engine/audio_output_timing.c",
        "tests/audio_output_timing/test_audio_output_timing.c",
    },
    "audio_output_remaining_delay_ms": {
        "firmware/main-deck-p4/components/audio_engine/include/audio_output_timing.h",
        "firmware/main-deck-p4/components/audio_engine/audio_output_timing.c",
        "tests/audio_output_timing/test_audio_output_timing.c",
    },
    "audio_mixer_mix_stereo": {
        "firmware/main-deck-p4/components/audio_engine/include/audio_mixer.h",
        "firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
        "tests/audio_mixer/test_audio_mixer.c",
    },
}


def text_files():
    for path in ROOT.rglob("*"):
        if not path.is_file() or ".git" in path.parts:
            continue
        if path.suffix.lower() not in {".c", ".h", ".ps1"}:
            continue
        yield path


for symbol, allowed in candidates.items():
    found = set()
    for path in text_files():
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if symbol in text:
            found.add(path.relative_to(ROOT).as_posix())
    unexpected = found - allowed
    missing = allowed - found
    print(f"{symbol}: {sorted(found)}")
    if unexpected:
        raise RuntimeError(f"{symbol}: unexpected call-sites/files: {sorted(unexpected)}")
    if missing:
        raise RuntimeError(f"{symbol}: expected audit files missing: {sorted(missing)}")


def remove_function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise RuntimeError(f"function signature not found: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise RuntimeError(f"opening brace not found: {signature}")
    depth = 0
    end = None
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end is None:
        raise RuntimeError(f"unterminated function: {signature}")
    while end < len(source) and source[end] == "\n":
        end += 1
    return source[:start] + source[end:]


scratch_h = ROOT / "firmware/main-deck-p4/components/audio_engine/include/audio_scratch_buffer.h"
text = scratch_h.read_text(encoding="utf-8")
for block in (
    """/* Append one stereo frame (overwrites the oldest once full). */
void audio_scratch_buffer_push(audio_scratch_buffer_t *b, int16_t left,
                               int16_t right);

""",
    """/* Map a track position (ms) to a stored frame index.
 * Returns false if the buffer is empty/unset, the position is in the future
 * (newer than the newest frame), or older than the buffered window. */
bool audio_scratch_buffer_index_for_ms(const audio_scratch_buffer_t *b,
                                       uint32_t pos_ms, uint32_t *out_index);

/* Read the stereo frame at an absolute store index [0,capacity).
 * Returns false on a null buffer/output or an out-of-range index. Whether the
 * index still holds live window data is the caller's concern (index_for_ms). */
bool audio_scratch_buffer_read(const audio_scratch_buffer_t *b, uint32_t index,
                               int16_t *out_left, int16_t *out_right);

""",
):
    if text.count(block) != 1:
        raise RuntimeError("scratch header legacy block did not match exactly")
    text = text.replace(block, "", 1)
text = text.replace(
    " * Phase 2 (current): capture only — the decode task appends every produced\n"
    " * frame here in addition to the PCM ring, keeping a rolling window that ends\n"
    " * near (slightly ahead of) the playhead. Normal playback is unchanged.\n"
    " *\n"
    " * Phases 3-4 will read this bidirectionally at a jog-driven read head to\n"
    " * produce the audible scratch. See docs/VINYL_SCRATCH_PLAN.md.\n",
    " * The decode path owns writes to the circular storage and the output task\n"
    " * reads it bidirectionally with `audio_scratch_buffer_read_frame_back()`.\n"
    " * This module owns reset/generation/accessor semantics, not a second writer API.\n",
    1,
)
scratch_h.write_text(text, encoding="utf-8")

scratch_c = ROOT / "firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c"
text = scratch_c.read_text(encoding="utf-8")
text = remove_function(text, "void audio_scratch_buffer_push(")
text = remove_function(text, "bool audio_scratch_buffer_index_for_ms(")
text = remove_function(text, "bool audio_scratch_buffer_read(")
scratch_c.write_text(text, encoding="utf-8")

(ROOT / "tests/audio_scratch_buffer/test_audio_scratch_buffer.c").write_text(r'''#include "audio_scratch_buffer.h"
#include <assert.h>
#include <stdio.h>

static int16_t g_store[8 * 2];

/* Mirrors the sole production writer's ring bookkeeping. The buffer component
 * intentionally exposes only reset/state accessors and frames-back reads. */
static void test_writer_push(audio_scratch_buffer_t *b, int16_t left, int16_t right)
{
    assert(b && b->frames && b->capacity > 0u);
    uint32_t idx = b->write_index;
    b->frames[idx * 2u] = left;
    b->frames[idx * 2u + 1u] = right;
    b->write_index = idx + 1u < b->capacity ? idx + 1u : 0u;
    if (b->filled < b->capacity) b->filled++;
}

static void test_reset_and_generation(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 8u);
    assert(audio_scratch_buffer_used(&b) == 0u);
    assert(audio_scratch_buffer_generation(&b) != 0u);

    uint32_t generation = audio_scratch_buffer_generation(&b);
    test_writer_push(&b, 1, -1);
    audio_scratch_buffer_set_sample_rate(&b, 48000u);
    audio_scratch_buffer_mark_newest_ms(&b, 1234u);
    audio_scratch_buffer_reset(&b);

    assert(audio_scratch_buffer_used(&b) == 0u);
    assert(audio_scratch_buffer_generation(&b) != generation);
    assert(b.sample_rate == 48000u);
    assert(!b.newest_valid);
}

static void test_frames_back_reads_live_window_after_wrap(void)
{
    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, g_store, 4u);
    for (int i = 0; i < 6; ++i) {
        test_writer_push(&b, (int16_t)(i + 1), (int16_t)-(i + 1));
    }
    assert(audio_scratch_buffer_used(&b) == 4u);

    int16_t left = 0;
    int16_t right = 0;
    assert(audio_scratch_buffer_read_frame_back(&b, 0u, &left, &right));
    assert(left == 6 && right == -6);
    assert(audio_scratch_buffer_read_frame_back(&b, 3u, &left, &right));
    assert(left == 3 && right == -3);
    assert(!audio_scratch_buffer_read_frame_back(&b, 4u, &left, &right));
}

static void test_frames_back_guards(void)
{
    int16_t left = 0;
    int16_t right = 0;
    assert(audio_scratch_buffer_used(NULL) == 0u);
    assert(audio_scratch_buffer_generation(NULL) == 0u);
    assert(!audio_scratch_buffer_read_frame_back(NULL, 0u, &left, &right));

    audio_scratch_buffer_t b;
    audio_scratch_buffer_init(&b, NULL, 4u);
    assert(!audio_scratch_buffer_read_frame_back(&b, 0u, &left, &right));
}

int main(void)
{
    test_reset_and_generation();
    test_frames_back_reads_live_window_after_wrap();
    test_frames_back_guards();
    puts("audio_scratch_buffer tests passed");
    return 0;
}
''', encoding="utf-8")

timing_h = ROOT / "firmware/main-deck-p4/components/audio_engine/include/audio_output_timing.h"
text = timing_h.read_text(encoding="utf-8")
for line in (
    "uint32_t audio_output_block_period_ms(uint32_t sample_rate);\n",
    "uint32_t audio_output_remaining_delay_ms(uint32_t sample_rate, uint32_t elapsed_us);\n",
):
    if text.count(line) != 1:
        raise RuntimeError(f"timing declaration mismatch: {line.strip()}")
    text = text.replace(line, "", 1)
timing_h.write_text(text, encoding="utf-8")

timing_c = ROOT / "firmware/main-deck-p4/components/audio_engine/audio_output_timing.c"
text = timing_c.read_text(encoding="utf-8")
text = remove_function(text, "uint32_t audio_output_block_period_ms(")
text = remove_function(text, "uint32_t audio_output_remaining_delay_ms(")
timing_c.write_text(text, encoding="utf-8")

(ROOT / "tests/audio_output_timing/test_audio_output_timing.c").write_text(r'''#include "audio_output_timing.h"

#include <assert.h>
#include <stdio.h>

static void test_block_period_us_uses_precise_ceil_division(void)
{
    assert(audio_output_block_period_us(48000) == 5334u);
    assert(audio_output_block_period_us(44100) == 5805u);
    assert(audio_output_block_period_us(32000) == 8000u);
    assert(audio_output_block_period_us(0) == 0u);
}

static void test_late_warning_threshold_allows_codec_write_pacing_slack(void)
{
    assert(audio_output_late_warning_threshold_us(48000) == 10668u);
    assert(audio_output_late_warning_threshold_us(44100) == 11610u);
    assert(audio_output_late_warning_threshold_us(32000) == 16000u);
    assert(audio_output_late_warning_threshold_us(0) == 0u);
}

static void test_continuous_output_periodically_forces_an_idle_tick(void)
{
    assert(!audio_output_should_force_idle(0u));
    assert(!audio_output_should_force_idle(AUDIO_OUTPUT_MAX_BUSY_BLOCKS - 1u));
    assert(audio_output_should_force_idle(AUDIO_OUTPUT_MAX_BUSY_BLOCKS));
    assert(audio_output_should_force_idle(AUDIO_OUTPUT_MAX_BUSY_BLOCKS + 1u));
}

int main(void)
{
    test_block_period_us_uses_precise_ceil_division();
    test_late_warning_threshold_allows_codec_write_pacing_slack();
    test_continuous_output_periodically_forces_an_idle_tick();
    puts("audio_output_timing tests passed");
    return 0;
}
''', encoding="utf-8")

mixer_h = ROOT / "firmware/main-deck-p4/components/audio_engine/include/audio_mixer.h"
text = mixer_h.read_text(encoding="utf-8")
block = """audio_mixer_frame_t audio_mixer_mix_stereo(audio_mixer_frame_t deck1,
                                           audio_mixer_frame_t deck2,
                                           float deck1_channel_gain,
                                           float deck2_channel_gain,
                                           uint16_t crossfader);
"""
if text.count(block) != 1:
    raise RuntimeError("mixer legacy declaration mismatch")
text = text.replace(block, "", 1)
mixer_h.write_text(text, encoding="utf-8")

mixer_c = ROOT / "firmware/main-deck-p4/components/audio_engine/audio_mixer.c"
text = mixer_c.read_text(encoding="utf-8")
text = remove_function(text, "audio_mixer_frame_t audio_mixer_mix_stereo(")
mixer_c.write_text(text, encoding="utf-8")

mixer_test = ROOT / "tests/audio_mixer/test_audio_mixer.c"
text = mixer_test.read_text(encoding="utf-8")
start = text.find("static void test_stereo_frame_uses_channel_and_crossfader_gains(void)")
end = text.find("static void test_apply_gain_scales_stereo_frame(void)", start)
if start < 0 or end < 0:
    raise RuntimeError("mixer test legacy block not found")
text = text[:start] + text[end:]
text = text.replace("    test_stereo_frame_uses_channel_and_crossfader_gains();\n", "", 1)
mixer_test.write_text(text, encoding="utf-8")

runner = ROOT / "tests/run_p4_host_tests_current.ps1"
text = runner.read_text(encoding="utf-8")
anchor = "\n'@\n\n$text = Get-Content -LiteralPath $source -Raw"
insert = r'''

Assert-FileDoesNotContain `
    -Name "confirmed dead audio APIs stay removed" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_scratch_buffer.h") `
    -LiteralPatterns @("audio_scratch_buffer_push", "audio_scratch_buffer_index_for_ms", "audio_scratch_buffer_read(")

Assert-FileDoesNotContain `
    -Name "retired output timing helpers stay removed" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_output_timing.h") `
    -LiteralPatterns @("audio_output_block_period_ms", "audio_output_remaining_delay_ms")

Assert-FileDoesNotContain `
    -Name "test-only stereo convenience API stays out of production" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_mixer.h") `
    -LiteralPatterns @("audio_mixer_mix_stereo")
'''
if text.count(anchor) != 1:
    raise RuntimeError("host runner audit gate anchor mismatch")
text = text.replace(anchor, insert + anchor, 1)
runner.write_text(text, encoding="utf-8")

for symbol in candidates:
    for path in text_files():
        rel = path.relative_to(ROOT).as_posix()
        if rel == "tests/run_p4_host_tests_current.ps1":
            continue
        try:
            content = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if symbol in content:
            raise RuntimeError(f"{symbol} remains in {rel}")

print("confirmed dead audio APIs removed")
