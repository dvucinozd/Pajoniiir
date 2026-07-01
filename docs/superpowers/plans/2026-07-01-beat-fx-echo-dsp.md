# Beat FX Echo DSP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first real-time-safe Beat FX Echo/Delay DSP slice for the existing DDJ-FLX4 Beat FX state model.

**Architecture:** P4 remains authoritative for Beat FX state. S3 MIDI mapping does not change. The new Echo DSP is a per-deck delay-line effect owned by `audio_engine`, selected when `deck_core` Beat FX effect is `DECK_CORE_BEAT_FX_ECHO`, and applied in the existing per-deck audio path before deck gain and master summing. The implementation must allocate all delay memory outside the audio hot path, bypass safely if allocation fails, and preserve the already verified FILTER behavior.

**Tech Stack:** ESP-IDF v5.5, C99, existing `audio_engine`, `audio_output_mixer`, `deck_core`, host GCC regression tests, P4 hardware smoke on COM15.

---

## Scope

Implement only this first Echo slice:

- Beat FX `ECHO` produces audible delay/echo.
- Target routing supports CH1, CH2, BOTH.
- ON/OFF and depth control whether the effect is active.
- Beat size maps to deterministic delay time.
- Clear/reset disables Echo and clears its delay buffers.
- Existing Beat FX FILTER continues to work.

Do not implement in this slice:

- Rekordbox-accurate Echo feedback curves.
- Tempo-analyzed BPM sync from track beatgrid.
- Post-fader echo tails after channel fader close.
- Pad FX audio behavior.
- New S3 MIDI mappings.

## Design constraints

- No dynamic allocation in `audio_output_mixer_next()` or `audio_output_mixer_next_full()`.
- No file I/O, logging, mutex waits, or blocking calls in the audio sample path.
- Delay buffer allocation must happen during `audio_engine_init()` or another explicit init path.
- If delay buffer allocation fails, Echo must bypass cleanly and diagnostics must expose the failure.
- First slice delay timing uses a fixed base BPM of 120 BPM:
  - `1/4` beat = 125 ms
  - `1/2` beat = 250 ms
  - `1` beat = 500 ms
  - `2` beats = 1000 ms
  - `4` beats = clamp to max delay, initially 1000 ms
- Max delay buffer is 1000 ms per deck at the output sample rate.
- Feedback is clamped to avoid runaway oscillation.

## File map

- Create: `firmware/main-deck-p4/components/audio_engine/include/audio_delay_fx.h`
  - Public delay FX state, config, init, reset, configure, process APIs.
- Create: `firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c`
  - Per-frame delay line DSP; no ESP dependencies in the sample processing function.
- Create: `tests/audio_delay_fx/test_audio_delay_fx.c`
  - Host tests for bypass, impulse delay, feedback decay, reset, allocation failure.
- Modify: `tests/run_p4_host_tests.ps1`
  - Add `audio_delay_fx` host test target.
- Modify: `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt`
  - Add `audio_delay_fx.c`.
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
  - Add delay FX fields to `audio_output_mixer_deck_t`.
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
  - Apply Beat FX Echo after deck EQ/filter/FILTER selection and before gain/summing.
- Modify: `tests/audio_output_mixer/test_audio_output_mixer.c`
  - Add target-routing tests for Echo.
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
  - Add Echo state to mixer/diagnostics snapshots.
  - Add `audio_engine_set_beat_fx_echo(...)`.
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
  - Own per-deck Echo states and buffers.
  - Configure Echo from Beat FX state.
  - Feed Echo state into output mixer deck descriptors.
- Modify: `tests/audio_engine/test_audio_engine.c`
  - Add Echo API/snapshot tests.
- Modify: `tests/deck_core_dual/stubs/audio_engine.h`
  - Add stub for `audio_engine_set_beat_fx_echo(...)`.
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
  - Verify `deck_core` calls Echo API when Beat FX effect is ECHO.
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Route `DECK_CORE_BEAT_FX_ECHO` to the new Echo API and disable FILTER.
- Modify: docs:
  - `README.md`
  - `docs/DEVELOPMENT_PLAN.md`
  - `docs/STARTUP_CHECKLIST.md`
  - `docs/DDJ_FLX4_MIDI_MAP.md`

---

## Task 1: Add host-tested `audio_delay_fx` DSP module

**Files:**
- Create: `firmware/main-deck-p4/components/audio_engine/include/audio_delay_fx.h`
- Create: `firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c`
- Create: `tests/audio_delay_fx/test_audio_delay_fx.c`
- Modify: `tests/run_p4_host_tests.ps1`
- Modify: `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt`

- [ ] **Step 1: Write the failing header/API test**

Create `tests/audio_delay_fx/test_audio_delay_fx.c`:

```c
#include "audio_delay_fx.h"
#include "audio_mixer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_disabled_bypasses_input(void)
{
    int16_t left[16] = { 0 };
    int16_t right[16] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 16u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = false,
        .delay_ms = 4,
        .wet_q15 = 16384,
        .feedback_q15 = 8192,
    };
    audio_delay_fx_configure(&fx, &cfg);

    audio_mixer_frame_t in = { .left = 1234, .right = -2345 };
    audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, in);
    assert(out.left == in.left);
    assert(out.right == in.right);
}

static void test_impulse_reappears_after_delay(void)
{
    int16_t left[16] = { 0 };
    int16_t right[16] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 16u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .delay_ms = 4,
        .wet_q15 = 16384,
        .feedback_q15 = 0,
    };
    audio_delay_fx_configure(&fx, &cfg);

    audio_mixer_frame_t out0 = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ .left = 10000, .right = 10000 });
    assert(out0.left == 10000);
    assert(out0.right == 10000);

    for (int i = 0; i < 3; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }

    audio_mixer_frame_t delayed = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });
    assert(delayed.left > 4500 && delayed.left < 5500);
    assert(delayed.right > 4500 && delayed.right < 5500);
}

static void test_feedback_decays_and_reset_clears_tail(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .delay_ms = 2,
        .wet_q15 = 16384,
        .feedback_q15 = 8192,
    };
    audio_delay_fx_configure(&fx, &cfg);

    (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ .left = 12000, .right = 12000 });
    (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });
    audio_mixer_frame_t first_echo = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });
    (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });
    audio_mixer_frame_t second_echo = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });

    assert(first_echo.left > second_echo.left);
    assert(second_echo.left > 0);

    audio_delay_fx_reset(&fx);
    for (int i = 0; i < 8; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t){ 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
}

static void test_null_or_zero_buffer_bypasses(void)
{
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, NULL, NULL, 0u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .delay_ms = 10,
        .wet_q15 = 32767,
        .feedback_q15 = 32767,
    };
    audio_delay_fx_configure(&fx, &cfg);

    audio_mixer_frame_t in = { .left = -3000, .right = 3000 };
    audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, in);
    assert(out.left == in.left);
    assert(out.right == in.right);
}

int main(void)
{
    test_disabled_bypasses_input();
    test_impulse_reappears_after_delay();
    test_feedback_decays_and_reset_clears_tail();
    test_null_or_zero_buffer_bypasses();
    puts("audio_delay_fx tests passed");
    return 0;
}
```

- [ ] **Step 2: Add the test target to `tests/run_p4_host_tests.ps1`**

Insert a target before `audio_output_mixer`:

```powershell
@{
    Name = "audio_delay_fx"
    Dir = "tests/audio_delay_fx"
    Target = "test_audio_delay_fx.exe"
    Args = @(
        "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
        "-I../../firmware/main-deck-p4/components/audio_engine/include",
        "-o", "test_audio_delay_fx.exe",
        "test_audio_delay_fx.c",
        "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c"
    )
},
```

- [ ] **Step 3: Run the test to verify RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: FAIL while building `audio_delay_fx` because `audio_delay_fx.h` / `audio_delay_fx.c` do not exist yet.

- [ ] **Step 4: Create `audio_delay_fx.h`**

Create `firmware/main-deck-p4/components/audio_engine/include/audio_delay_fx.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_mixer.h"

typedef struct {
    bool enabled;
    uint32_t delay_ms;
    uint16_t wet_q15;
    uint16_t feedback_q15;
} audio_delay_fx_config_t;

typedef struct {
    int16_t *left;
    int16_t *right;
    uint32_t capacity_frames;
    uint32_t sample_rate;
    uint32_t write_index;
    uint32_t delay_frames;
    audio_delay_fx_config_t config;
    bool allocated;
} audio_delay_fx_t;

void audio_delay_fx_init(audio_delay_fx_t *fx,
                         int16_t *left,
                         int16_t *right,
                         uint32_t capacity_frames,
                         uint32_t sample_rate);
void audio_delay_fx_reset(audio_delay_fx_t *fx);
void audio_delay_fx_configure(audio_delay_fx_t *fx, const audio_delay_fx_config_t *config);
audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in);
bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx);
uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx);
```

- [ ] **Step 5: Create `audio_delay_fx.c`**

Create `firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c`:

```c
#include "audio_delay_fx.h"

#include <string.h>

static int16_t clamp_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static int32_t q15_mul_i16(int16_t sample, uint16_t gain_q15)
{
    return ((int32_t)sample * (int32_t)gain_q15) >> 15;
}

void audio_delay_fx_init(audio_delay_fx_t *fx,
                         int16_t *left,
                         int16_t *right,
                         uint32_t capacity_frames,
                         uint32_t sample_rate)
{
    if (!fx) return;
    memset(fx, 0, sizeof(*fx));
    fx->left = left;
    fx->right = right;
    fx->capacity_frames = capacity_frames;
    fx->sample_rate = sample_rate;
    fx->allocated = left && right && capacity_frames > 1u && sample_rate > 0u;
    audio_delay_fx_reset(fx);
}

void audio_delay_fx_reset(audio_delay_fx_t *fx)
{
    if (!fx) return;
    fx->write_index = 0;
    if (fx->left && fx->capacity_frames > 0u) {
        memset(fx->left, 0, fx->capacity_frames * sizeof(fx->left[0]));
    }
    if (fx->right && fx->capacity_frames > 0u) {
        memset(fx->right, 0, fx->capacity_frames * sizeof(fx->right[0]));
    }
}

void audio_delay_fx_configure(audio_delay_fx_t *fx, const audio_delay_fx_config_t *config)
{
    if (!fx || !config) return;
    fx->config = *config;
    if (fx->config.wet_q15 > 32767u) fx->config.wet_q15 = 32767u;
    if (fx->config.feedback_q15 > 24576u) fx->config.feedback_q15 = 24576u;

    uint64_t frames = ((uint64_t)fx->sample_rate * (uint64_t)fx->config.delay_ms + 999u) / 1000u;
    if (frames < 1u) frames = 1u;
    if (fx->capacity_frames > 0u && frames >= fx->capacity_frames) {
        frames = fx->capacity_frames - 1u;
    }
    fx->delay_frames = (uint32_t)frames;
}

audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in)
{
    if (!fx || !fx->allocated || !fx->config.enabled || fx->delay_frames == 0u) {
        return in;
    }

    uint32_t read_index = (fx->write_index + fx->capacity_frames - fx->delay_frames) % fx->capacity_frames;
    int16_t delayed_l = fx->left[read_index];
    int16_t delayed_r = fx->right[read_index];

    int32_t out_l = (int32_t)in.left + q15_mul_i16(delayed_l, fx->config.wet_q15);
    int32_t out_r = (int32_t)in.right + q15_mul_i16(delayed_r, fx->config.wet_q15);

    fx->left[fx->write_index] = clamp_i16((int32_t)in.left + q15_mul_i16(delayed_l, fx->config.feedback_q15));
    fx->right[fx->write_index] = clamp_i16((int32_t)in.right + q15_mul_i16(delayed_r, fx->config.feedback_q15));
    fx->write_index = (fx->write_index + 1u) % fx->capacity_frames;

    return (audio_mixer_frame_t) {
        .left = clamp_i16(out_l),
        .right = clamp_i16(out_r),
    };
}

bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx)
{
    return fx && fx->allocated;
}

uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx)
{
    return fx ? fx->config.delay_ms : 0u;
}
```

- [ ] **Step 6: Add component source**

Modify `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt` and add:

```cmake
"audio_delay_fx.c"
```

to the component source list.

- [ ] **Step 7: Run GREEN test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `audio_delay_fx tests passed`. If later tests fail because the new module is not wired yet, fix only compile/link omissions from this task.

- [ ] **Step 8: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine/include/audio_delay_fx.h firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c firmware/main-deck-p4/components/audio_engine/CMakeLists.txt tests/audio_delay_fx/test_audio_delay_fx.c tests/run_p4_host_tests.ps1
git commit -m "feat(audio): add beat fx delay line"
```

---

## Task 2: Wire Echo into `audio_output_mixer`

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- Modify: `tests/audio_output_mixer/test_audio_output_mixer.c`

- [ ] **Step 1: Write failing mixer test**

Add to `tests/audio_output_mixer/test_audio_output_mixer.c`:

```c
static void test_beat_fx_echo_applies_only_to_target_deck(void)
{
    audio_mixer_frame_t deck0_frames[8] = {
        { .left = 10000, .right = 10000 },
    };
    audio_mixer_frame_t deck1_frames[8] = {
        { .left = 7000, .right = 7000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 8, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 8, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    int16_t echo_l[16] = { 0 };
    int16_t echo_r[16] = { 0 };
    audio_delay_fx_t echo;
    audio_delay_fx_init(&echo, echo_l, echo_r, 16u, 1000u);
    audio_delay_fx_configure(&echo, &(audio_delay_fx_config_t) {
        .enabled = true,
        .delay_ms = 2,
        .wet_q15 = 16384,
        .feedback_q15 = 0,
    });
    deck0.beat_fx_echo = &echo;
    deck0.beat_fx_echo_enabled = true;

    prime_output_mixer(&deck0, &deck1);
    (void)audio_output_mixer_next_full(&deck0, &deck1, false, false,
                                       AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                       NULL, NULL, NULL);
    (void)audio_output_mixer_next_full(&deck0, &deck1, false, false,
                                       AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                       NULL, NULL, NULL);
    audio_output_mix_result_t delayed = audio_output_mixer_next_full(&deck0, &deck1,
                                                                     false, false,
                                                                     AUDIO_OUTPUT_HEADPHONE_MASTER_MONO,
                                                                     NULL, NULL, NULL);

    assert(delayed.deck_frame[0].left > 0);
    assert(delayed.deck_frame[1].left == 0);
}
```

Add the function to `main()`:

```c
test_beat_fx_echo_applies_only_to_target_deck();
```

- [ ] **Step 2: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: FAIL because `audio_output_mixer_deck_t` does not yet expose `beat_fx_echo`.

- [ ] **Step 3: Add Echo fields to mixer deck descriptor**

Modify `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`:

```c
#include "audio_delay_fx.h"
```

Add fields after Beat FX filter fields:

```c
audio_delay_fx_t *beat_fx_echo;
bool beat_fx_echo_enabled;
```

- [ ] **Step 4: Apply Echo in the mixer**

Modify `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`:

```c
static audio_mixer_frame_t apply_deck_beat_fx_echo(const audio_output_mixer_deck_t *deck,
                                                   audio_mixer_frame_t frame)
{
    if (!deck || !deck->beat_fx_echo || !deck->beat_fx_echo_enabled) {
        return frame;
    }
    return audio_delay_fx_process_frame(deck->beat_fx_echo, frame);
}
```

Then replace the per-deck processing chain in both `audio_output_mixer_next()` and `audio_output_mixer_next_full()` with:

```c
audio_mixer_frame_t frame0 = apply_deck_beat_fx_echo(deck0,
    apply_deck_beat_fx_filter(deck0,
        apply_deck_filter(deck0, apply_deck_eq(deck0, next_deck_frame(deck0, &consumed0)))));
audio_mixer_frame_t frame1 = apply_deck_beat_fx_echo(deck1,
    apply_deck_beat_fx_filter(deck1,
        apply_deck_filter(deck1, apply_deck_eq(deck1, next_deck_frame(deck1, &consumed1)))));
```

- [ ] **Step 5: Run GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `audio_output_mixer tests passed`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c tests/audio_output_mixer/test_audio_output_mixer.c
git commit -m "feat(audio): apply beat fx echo in mixer"
```

---

## Task 3: Add `audio_engine` Echo ownership, allocation, API, snapshots

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `tests/audio_engine/test_audio_engine.c`

- [ ] **Step 1: Write failing `audio_engine` test**

Add near the existing Beat FX filter test in `tests/audio_engine/test_audio_engine.c`:

```c
EXPECT(!snapshot.beat_fx_echo_enabled[0], "Beat FX echo defaults off for deck 0");
EXPECT(!snapshot.beat_fx_echo_enabled[1], "Beat FX echo defaults off for deck 1");
EXPECT(audio_engine_set_beat_fx_echo(AUDIO_ENGINE_BEAT_FX_TARGET_CH1,
                                     64,
                                     500,
                                     true) == ESP_OK,
       "Beat FX echo accepts CH1 target");
audio_engine_get_mixer_snapshot(&snapshot);
EXPECT(snapshot.beat_fx_echo_enabled[0], "Beat FX echo enables targeted deck 0");
EXPECT(!snapshot.beat_fx_echo_enabled[1], "Beat FX echo leaves untargeted deck 1 off");
EXPECT(snapshot.beat_fx_echo_delay_ms[0] == 500, "Beat FX echo stores delay ms");
EXPECT(audio_engine_set_beat_fx_echo(AUDIO_ENGINE_BEAT_FX_TARGET_BOTH,
                                     0,
                                     250,
                                     true) == ESP_OK,
       "Beat FX echo depth zero bypasses both decks");
audio_engine_get_mixer_snapshot(&snapshot);
EXPECT(!snapshot.beat_fx_echo_enabled[0], "Beat FX echo zero depth disables deck 0");
EXPECT(!snapshot.beat_fx_echo_enabled[1], "Beat FX echo zero depth disables deck 1");
```

- [ ] **Step 2: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: FAIL because Echo snapshot fields and `audio_engine_set_beat_fx_echo()` do not exist.

- [ ] **Step 3: Extend public snapshot/API**

Modify `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`:

Add to `audio_engine_mixer_snapshot_t`:

```c
bool beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
uint32_t beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
bool beat_fx_echo_allocated[AUDIO_ENGINE_DECK_COUNT];
```

Add to `audio_engine_diagnostics_snapshot_t`:

```c
bool beat_fx_echo_allocated[AUDIO_ENGINE_DECK_COUNT];
bool beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
uint32_t beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
```

Add API:

```c
esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                        uint8_t depth,
                                        uint32_t delay_ms,
                                        bool enabled);
```

- [ ] **Step 4: Add owned Echo state in `audio_engine.c`**

Add includes:

```c
#include "audio_delay_fx.h"
#include "esp_heap_caps.h"
```

Add constants and state:

```c
#define AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS 1000u
#define AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE 48000u

static audio_delay_fx_t s_beat_fx_echo[AUDIO_ENGINE_DECK_COUNT];
static int16_t *s_beat_fx_echo_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t *s_beat_fx_echo_right[AUDIO_ENGINE_DECK_COUNT];
static bool s_beat_fx_echo_enabled[AUDIO_ENGINE_DECK_COUNT];
static uint32_t s_beat_fx_echo_delay_ms[AUDIO_ENGINE_DECK_COUNT];
```

Add helper:

```c
static uint32_t beat_fx_echo_capacity_frames(void)
{
    return (AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE *
            AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS) / 1000u;
}

static void init_beat_fx_echo_buffers(void)
{
    uint32_t frames = beat_fx_echo_capacity_frames();
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        if (!s_beat_fx_echo_left[deck]) {
            s_beat_fx_echo_left[deck] = heap_caps_calloc(frames, sizeof(int16_t),
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_beat_fx_echo_right[deck]) {
            s_beat_fx_echo_right[deck] = heap_caps_calloc(frames, sizeof(int16_t),
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        audio_delay_fx_init(&s_beat_fx_echo[deck],
                            s_beat_fx_echo_left[deck],
                            s_beat_fx_echo_right[deck],
                            frames,
                            AUDIO_ENGINE_BEAT_FX_ECHO_FALLBACK_SAMPLE_RATE);
        s_beat_fx_echo_enabled[deck] = false;
        s_beat_fx_echo_delay_ms[deck] = 0;
    }
}
```

Call `init_beat_fx_echo_buffers()` from `audio_engine_init()` after existing audio state init.

- [ ] **Step 5: Implement Echo config API**

Add:

```c
static uint16_t beat_fx_echo_wet_from_depth(uint8_t depth)
{
    return (uint16_t)(((uint32_t)depth * 19660u) / 127u); // max about 0.60
}

static uint16_t beat_fx_echo_feedback_from_depth(uint8_t depth)
{
    return (uint16_t)(((uint32_t)depth * 14745u) / 127u); // max about 0.45
}

esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                        uint8_t depth,
                                        uint32_t delay_ms,
                                        bool enabled)
{
    if (target != AUDIO_ENGINE_BEAT_FX_TARGET_CH1 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_CH2 &&
        target != AUDIO_ENGINE_BEAT_FX_TARGET_BOTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (delay_ms == 0u) {
        delay_ms = 1u;
    }
    if (delay_ms > AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS) {
        delay_ms = AUDIO_ENGINE_BEAT_FX_ECHO_MAX_DELAY_MS;
    }

    bool active = enabled && depth > 0u;
    for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
        bool deck_enabled = active &&
                            beat_fx_target_includes_deck(target, deck) &&
                            audio_delay_fx_is_allocated(&s_beat_fx_echo[deck]);
        s_beat_fx_echo_enabled[deck] = deck_enabled;
        s_beat_fx_echo_delay_ms[deck] = deck_enabled ? delay_ms : 0u;
        audio_delay_fx_configure(&s_beat_fx_echo[deck], &(audio_delay_fx_config_t) {
            .enabled = deck_enabled,
            .delay_ms = delay_ms,
            .wet_q15 = beat_fx_echo_wet_from_depth(depth),
            .feedback_q15 = beat_fx_echo_feedback_from_depth(depth),
        });
        if (!deck_enabled) {
            audio_delay_fx_reset(&s_beat_fx_echo[deck]);
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 6: Feed Echo into mixer descriptors and snapshots**

Where `audio_output_mixer_deck_t` is populated, add:

```c
.beat_fx_echo = &s_beat_fx_echo[deck],
.beat_fx_echo_enabled = s_beat_fx_echo_enabled[deck],
```

In `audio_engine_get_mixer_snapshot()` and diagnostics snapshot population, add:

```c
out_snapshot->beat_fx_echo_enabled[deck] = s_beat_fx_echo_enabled[deck];
out_snapshot->beat_fx_echo_delay_ms[deck] = s_beat_fx_echo_delay_ms[deck];
out_snapshot->beat_fx_echo_allocated[deck] = audio_delay_fx_is_allocated(&s_beat_fx_echo[deck]);
```

- [ ] **Step 7: Run GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `audio_engine tests` and all existing P4 host tests pass.

- [ ] **Step 8: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine/include/audio_engine.h firmware/main-deck-p4/components/audio_engine/audio_engine.c tests/audio_engine/test_audio_engine.c
git commit -m "feat(audio): own beat fx echo state"
```

---

## Task 4: Route `deck_core` Beat FX ECHO to `audio_engine`

**Files:**
- Modify: `tests/deck_core_dual/stubs/audio_engine.h`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`

- [ ] **Step 1: Extend deck_core audio stub**

Modify `tests/deck_core_dual/stubs/audio_engine.h`:

```c
extern int audio_engine_stub_beat_fx_echo_target;
extern int audio_engine_stub_beat_fx_echo_depth;
extern uint32_t audio_engine_stub_beat_fx_echo_delay_ms;
extern bool audio_engine_stub_beat_fx_echo_enabled;
extern int audio_engine_stub_beat_fx_echo_set_count;

static inline esp_err_t audio_engine_set_beat_fx_echo(audio_engine_beat_fx_target_t target,
                                                      uint8_t depth,
                                                      uint32_t delay_ms,
                                                      bool enabled)
{
    audio_engine_stub_beat_fx_echo_target = (int)target;
    audio_engine_stub_beat_fx_echo_depth = (int)depth;
    audio_engine_stub_beat_fx_echo_delay_ms = delay_ms;
    audio_engine_stub_beat_fx_echo_enabled = enabled;
    audio_engine_stub_beat_fx_echo_set_count++;
    return ESP_OK;
}
```

Add globals/reset in `tests/deck_core_dual/test_deck_core_dual.c`:

```c
int audio_engine_stub_beat_fx_echo_target;
int audio_engine_stub_beat_fx_echo_depth;
uint32_t audio_engine_stub_beat_fx_echo_delay_ms;
bool audio_engine_stub_beat_fx_echo_enabled;
int audio_engine_stub_beat_fx_echo_set_count;
```

Reset them in `reset_audio_engine_stub()`:

```c
audio_engine_stub_beat_fx_echo_target = -1;
audio_engine_stub_beat_fx_echo_depth = -1;
audio_engine_stub_beat_fx_echo_delay_ms = 0;
audio_engine_stub_beat_fx_echo_enabled = false;
audio_engine_stub_beat_fx_echo_set_count = 0;
```

- [ ] **Step 2: Write failing deck_core Echo routing test**

Add:

```c
static void test_beat_fx_echo_state_updates_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t next = beat_fx_button(CTRL_ID_BEAT_FX_SELECT_NEXT, 1);
    ctrl_event_t target = beat_fx_button(CTRL_ID_BEAT_FX_TARGET, CTRL_BEAT_FX_TARGET_CH1);
    ctrl_event_t beat_inc = beat_fx_button(CTRL_ID_BEAT_FX_BEAT_INC, 1);
    ctrl_event_t depth = beat_fx_depth(96);
    ctrl_event_t on = beat_fx_button(CTRL_ID_BEAT_FX_ON, 1);

    deck_core_test_apply_event(&next);      // FILTER -> ECHO
    deck_core_test_apply_event(&target);
    deck_core_test_apply_event(&beat_inc);  // 1 beat -> 2 beats
    deck_core_test_apply_event(&depth);
    deck_core_test_apply_event(&on);

    assert(audio_engine_stub_beat_fx_echo_set_count > 0);
    assert(audio_engine_stub_beat_fx_echo_target == AUDIO_ENGINE_BEAT_FX_TARGET_CH1);
    assert(audio_engine_stub_beat_fx_echo_depth == 96);
    assert(audio_engine_stub_beat_fx_echo_delay_ms == 1000);
    assert(audio_engine_stub_beat_fx_echo_enabled);
    assert(!audio_engine_stub_beat_fx_filter_enabled);

    ctrl_event_t clear = beat_fx_button(CTRL_ID_BEAT_FX_CLEAR, 1);
    deck_core_test_apply_event(&clear);
    assert(!audio_engine_stub_beat_fx_echo_enabled);
}
```

Add to `main()`:

```c
test_beat_fx_echo_state_updates_audio_engine();
```

- [ ] **Step 3: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: FAIL because `deck_core` does not call Echo API.

- [ ] **Step 4: Implement Beat size to delay mapping**

Add in `firmware/main-deck-p4/components/deck_core/deck_core.c`:

```c
static uint32_t beat_fx_delay_ms(deck_core_beat_fx_beat_t beat)
{
    switch (beat) {
    case DECK_CORE_BEAT_FX_BEAT_1_4:
        return 125u;
    case DECK_CORE_BEAT_FX_BEAT_1_2:
        return 250u;
    case DECK_CORE_BEAT_FX_BEAT_1:
        return 500u;
    case DECK_CORE_BEAT_FX_BEAT_2:
        return 1000u;
    case DECK_CORE_BEAT_FX_BEAT_4:
        return 1000u;
    default:
        return 500u;
    }
}
```

- [ ] **Step 5: Update `sync_beat_fx_audio_state()`**

Replace current FILTER-only sync with:

```c
static void sync_beat_fx_audio_state(void)
{
    audio_engine_beat_fx_target_t target = beat_fx_audio_target(s_beat_fx.target);
    bool filter_enabled = s_beat_fx.enabled &&
                          s_beat_fx.effect == DECK_CORE_BEAT_FX_FILTER;
    bool echo_enabled = s_beat_fx.enabled &&
                        s_beat_fx.effect == DECK_CORE_BEAT_FX_ECHO;

    audio_engine_set_beat_fx_filter(target,
                                    s_beat_fx.depth,
                                    filter_enabled);
    audio_engine_set_beat_fx_echo(target,
                                  s_beat_fx.depth,
                                  beat_fx_delay_ms(s_beat_fx.beat),
                                  echo_enabled);
}
```

- [ ] **Step 6: Run GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `deck_core_dual tests passed`.

- [ ] **Step 7: Commit**

```powershell
git add tests/deck_core_dual/stubs/audio_engine.h tests/deck_core_dual/test_deck_core_dual.c firmware/main-deck-p4/components/deck_core/deck_core.c
git commit -m "feat(deck): route beat fx echo"
```

---

## Task 5: Diagnostics and docs

**Files:**
- Modify: `firmware/main-deck-p4/components/web_server/include/web_api_helpers.h`
- Modify: `firmware/main-deck-p4/components/web_server/web_api_helpers.c`
- Modify: `firmware/main-deck-p4/components/web_server/web_server.c`
- Modify: `tests/web_api_helpers/test_web_api_helpers.c`
- Modify: `README.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`

- [ ] **Step 1: Decide diagnostics exposure**

Expose Echo diagnostics in `/api/status` under existing `diagnostics`, not in the high-rate UI frame:

```json
"beat_fx_echo":{
  "allocated":[true,true],
  "enabled":[false,true],
  "delay_ms":[0,500]
}
```

- [ ] **Step 2: Add failing web helper test**

Add to `tests/web_api_helpers/test_web_api_helpers.c`:

```c
static void test_beat_fx_echo_diag_json_formats_status_block(void)
{
    char out[160] = {0};
    int n = web_api_format_beat_fx_echo_diag_json(out,
                                                  sizeof(out),
                                                  true,
                                                  true,
                                                  false,
                                                  true,
                                                  0,
                                                  500);
    assert(n > 0);
    assert(strcmp(out, "\"beat_fx_echo\":{\"allocated\":[true,true],\"enabled\":[false,true],\"delay_ms\":[0,500]}") == 0);
}
```

- [ ] **Step 3: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: FAIL because helper does not exist.

- [ ] **Step 4: Add helper API and implementation**

Add to `web_api_helpers.h`:

```c
int web_api_format_beat_fx_echo_diag_json(char *dst,
                                          size_t dst_size,
                                          bool allocated0,
                                          bool allocated1,
                                          bool enabled0,
                                          bool enabled1,
                                          unsigned delay0_ms,
                                          unsigned delay1_ms);
```

Implement in `web_api_helpers.c`:

```c
int web_api_format_beat_fx_echo_diag_json(char *dst,
                                          size_t dst_size,
                                          bool allocated0,
                                          bool allocated1,
                                          bool enabled0,
                                          bool enabled1,
                                          unsigned delay0_ms,
                                          unsigned delay1_ms)
{
    if (!dst || dst_size == 0u) {
        return -1;
    }
    return snprintf(dst,
                    dst_size,
                    "\"beat_fx_echo\":{\"allocated\":[%s,%s],\"enabled\":[%s,%s],\"delay_ms\":[%u,%u]}",
                    allocated0 ? "true" : "false",
                    allocated1 ? "true" : "false",
                    enabled0 ? "true" : "false",
                    enabled1 ? "true" : "false",
                    delay0_ms,
                    delay1_ms);
}
```

- [ ] **Step 5: Wire helper into `/api/status`**

In `web_server.c`, after reading `audio_engine_diagnostics_snapshot_t diag`, format the JSON block and append it to the diagnostics object. Keep response bounded; if the existing response buffer is tight, increase it deliberately and host-test truncation behavior.

- [ ] **Step 6: Update docs**

Update active docs with:

- Echo DSP first slice implemented.
- Fixed 120 BPM fallback timing for first slice.
- Echo/delay no longer fully deferred; accurate BPM-synced echo remains deferred.
- Hardware smoke checklist pending until P4 COM15 verification.

Specific wording for `docs/DDJ_FLX4_MIDI_MAP.md` Beat FX rows:

```markdown
FILTER and first Echo DSP slices implemented; Echo uses fixed 120 BPM fallback timing in this slice; BPM-synced rekordbox-style delay remains deferred.
```

- [ ] **Step 7: Run GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
git diff --check
```

Expected: P4 host tests pass and `git diff --check` exit 0.

- [ ] **Step 8: Commit**

```powershell
git add firmware/main-deck-p4/components/web_server/include/web_api_helpers.h firmware/main-deck-p4/components/web_server/web_api_helpers.c firmware/main-deck-p4/components/web_server/web_server.c tests/web_api_helpers/test_web_api_helpers.c README.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md docs/DDJ_FLX4_MIDI_MAP.md
git commit -m "docs(audio): record beat fx echo slice"
```

---

## Task 6: Firmware build and hardware smoke

**Files:**
- No source edits unless build/smoke exposes a bug.

- [ ] **Step 1: Run full P4 host tests**

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

- [ ] **Step 2: Build P4 firmware**

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: `Project build complete.`

- [ ] **Step 3: Flash P4**

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash
```

Expected: flash completes and hard resets.

- [ ] **Step 4: Hardware smoke checklist**

Manual checks with FLX4 connected:

1. Boot P4 and S3; verify controller remains responsive.
2. Load one track on Deck 1.
3. Select Beat FX `ECHO`.
4. Set target CH1.
5. Set depth around 50%.
6. Press Beat FX ON:
   - audible echo appears on Deck 1;
   - ON/OFF LED follows state;
   - audio speed does not slow down;
   - waveform remains usable.
7. Change beat size:
   - delay time changes in coarse steps;
   - no reboot/watchdog.
8. Change target CH2 and BOTH:
   - Echo applies only to selected target.
9. Press Shift+ON/OFF clear:
   - Echo disables;
   - tail clears;
   - LED off.
10. Play both decks with Echo enabled on one deck:
    - audio does not crackle;
    - `/api/status` or serial diagnostics show no sustained output late count increase.

- [ ] **Step 5: Capture 45-60 seconds of P4 logs**

Use existing COM15 monitor/capture workflow. Save under `logs/`, but do not commit logs unless explicitly requested.

- [x] **Step 6: If hardware smoke passes, close docs**

Update:

- `docs/STARTUP_CHECKLIST.md`
- `docs/DEVELOPMENT_PLAN.md`
- `docs/DDJ_FLX4_MIDI_MAP.md`

Echo first-slice smoke status was closed on 2026-07-01 after hardware smoke
confirmed audible Echo, gradual FILTER depth response, and CH1/CH2/BOTH target
routing. Live BPM-synced delay calculation remains deferred.

- [ ] **Step 7: Final docs commit and push**

```powershell
git diff --check
git status --short
git add <changed files>
git commit -m "feat(audio): add beat fx echo dsp"
git push
```

Expected: only source/docs/test changes committed; `logs/` remains untracked unless the user explicitly requests committing logs.

---

## Risk controls

- If PSRAM allocation fails, do not crash; bypass Echo and expose `allocated=false`.
- If host tests show clipping, reduce max wet from `19660` to `16384` and max feedback from `14745` to `12288`.
- If P4 hardware smoke shows waveform stutter, first inspect log verbosity and `DIAG_OUTPUT_LATE_COUNT`; do not increase Echo complexity.
- If dual-deck playback crackles, disable Echo on BOTH target and test CH1-only to separate CPU load from summing/clipping.
- If boot loop appears, inspect COM15 logs before reverting; likely causes are allocation failure handling or `/api/status` JSON formatting.

## Verification summary before marking complete

Required before claiming done:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
git diff --check

$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Required hardware verification before closing smoke:

- P4 flashed on COM15.
- Echo audible.
- Target CH1/CH2/BOTH checked.
- ON/OFF LED still follows state.
- Clear/reset disables Echo and clears tail.
- Dual-deck playback remains normal.
- No reboot/watchdog.
