# Pad FX DSP Slice and Smart CFX Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a safe first Pad FX DSP path and refine Smart CFX into a more musical, testable macro-filter behavior without destabilizing dual-deck playback.

**Architecture:** P4 remains authoritative for deck state, pad mode state, Smart CFX state, and DSP routing. The first Pad FX slice is implemented as P4/audio-engine behavior behind explicit APIs and `CTRL_PAD_ACTION` handling; S3 Pad FX pad address work is kept as a separate hardware verification gate because the current documentation marks Pad FX pad input ranges as ambiguous. Smart CFX refinement reuses the existing deck-local filter path but adds a dedicated macro curve and optional telemetry so it does not change Beat FX FILTER/ECHO behavior.

**Tech Stack:** ESP-IDF v5.5, C99, P4 `audio_engine`, `audio_output_mixer`, `deck_core`, existing `0xA5` control link, host GCC tests via `tests/run_p4_host_tests.ps1` and `tests/run_s3_host_tests.ps1`, P4 hardware smoke on COM15.

---

## Scope boundaries

### In scope

- P4-only Pad FX DSP slice with deterministic host tests.
- P4 `deck_core` handling for `CTRL_PAD_MODE_PAD_FX1` / `CTRL_PAD_MODE_PAD_FX2` synthetic `CTRL_PAD_ACTION` events.
- Pad FX LED/state model only where state is already known by P4; no speculative LED addresses.
- Smart CFX curve refinement using existing FLX4 channel filter controls.
- Documentation updates for implemented and still-gated hardware behavior.

### Out of scope for this plan

- New unverified S3 Pad FX pad input ranges.
- Rekordbox/Mixxx full Pad FX parity.
- Additional heavy DSP such as reverb, flanger, or time-stretch effects.
- Changing Beat FX FILTER/ECHO behavior.
- Changing waveform rendering or audio scheduling.

---

## Existing facts to preserve

- `firmware/main-deck-p4/components/audio_engine/audio_filter.c` is already used by:
  - Smart CFX deck filter path.
  - Beat FX FILTER path.
- `firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c` is already used by Beat FX Echo.
- `firmware/main-deck-p4/components/deck_core/deck_core.c` already tracks:
  - `CTRL_PAD_MODE_PAD_FX1`
  - `CTRL_PAD_MODE_PAD_FX2`
  - `CTRL_ID_SMART_CFX`
- `docs/DDJ_FLX4_MIDI_MAP.md` currently marks Pad FX pad actions as deferred/ambiguous.
- `tests/run_p4_host_tests.ps1` already builds the relevant audio/deck/ui host tests.

---

## Proposed first-slice behavior

### Pad FX DSP slice

Use momentary pad behavior, P4-owned per deck:

| Mode | Pad | First-slice behavior | Rationale |
|---|---:|---|---|
| PAD FX1 | 1 | Momentary low-pass filter, medium depth | Uses proven filter DSP |
| PAD FX1 | 2 | Momentary high-pass filter, medium depth | Uses proven filter DSP |
| PAD FX1 | 3 | Momentary Echo 1/2 beat equivalent, medium depth | Uses proven delay DSP |
| PAD FX1 | 4 | Momentary Echo 1 beat equivalent, medium depth | Uses proven delay DSP |
| PAD FX1 | 5-8 | No-op with state clear on release | Safe, deterministic |
| PAD FX2 | 1 | Momentary stronger low-pass filter | Variation of proven filter DSP |
| PAD FX2 | 2 | Momentary stronger high-pass filter | Variation of proven filter DSP |
| PAD FX2 | 3 | Momentary Echo 1/4 beat equivalent | Short delay |
| PAD FX2 | 4 | Momentary Echo 2 beat equivalent | Long delay |
| PAD FX2 | 5-8 | No-op with state clear on release | Safe, deterministic |

Only one Pad FX slot is active per deck at a time in this first slice. Pressing a new Pad FX pad replaces the previous Pad FX state on that deck. Releasing the active pad clears Pad FX on that deck. Releasing a non-active pad does not clear the active pad.

### Smart CFX refinement

Keep Smart CFX as a toggle. When enabled, deck filter knobs remain the macro input, but the raw filter value is transformed through a musical curve before reaching DSP:

- center deadband remains stable around `AUDIO_FILTER_RAW_CENTER`;
- near-center movement is gentle;
- outer travel reaches current full low-pass/high-pass depth;
- Smart CFX remains deck-local because channel filter knobs are per deck;
- Beat FX FILTER remains unchanged.

---

## File structure

### New files

- `firmware/main-deck-p4/components/audio_engine/include/audio_pad_fx.h`
  - Defines Pad FX state/config and small processing API.
- `firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c`
  - Implements first-slice Pad FX routing using existing `audio_filter` and `audio_delay_fx`.
- `tests/audio_pad_fx/test_audio_pad_fx.c`
  - Host tests for Pad FX state, momentary behavior, filter and echo processing.

### Modified files

- `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt`
  - Adds `audio_pad_fx.c`.
- `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
  - Adds Pad FX public API and diagnostics fields.
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
  - Owns per-deck Pad FX state and wires Pad FX into output descriptors.
- `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
  - Adds Pad FX pointers/enabled flags to per-deck mixer descriptor.
- `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
  - Applies Pad FX after deck EQ/filter and before Beat FX/Echo/gain.
- `firmware/main-deck-p4/components/audio_engine/audio_filter.c`
  - Adds optional helper for Smart CFX raw curve only if it can be shared cleanly.
- `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Routes PAD_FX1/PAD_FX2 pad actions to audio engine.
- `tests/audio_output_mixer/test_audio_output_mixer.c`
  - Verifies Pad FX applies only to targeted deck.
- `tests/audio_engine/test_audio_engine.c`
  - Verifies Pad FX API state and Smart CFX curve snapshot.
- `tests/deck_core_dual/test_deck_core_dual.c`
  - Verifies deck_core translates Pad FX pad press/release into audio engine calls.
- `tests/deck_core_dual/stubs/audio_engine.h`
  - Adds Pad FX API stubs.
- `tests/run_p4_host_tests.ps1`
  - Adds `audio_pad_fx` host test.
- `docs/DDJ_FLX4_MIDI_MAP.md`
  - Updates Pad FX rows to distinguish P4 DSP implemented from S3 input capture pending.
- `docs/DEVELOPMENT_PLAN.md`
  - Records Pad FX first slice and Smart CFX refinement.
- `docs/STARTUP_CHECKLIST.md`
  - Adds smoke checklist.
- `README.md`
  - Adds one short status paragraph.

---

## Task 0: Pre-flight checks

**Files:**
- Read: `docs/DDJ_FLX4_MIDI_MAP.md`
- Read: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Read: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- Read: `firmware/main-deck-p4/components/deck_core/deck_core.c`

- [ ] **Step 1: Confirm clean worktree**

Run:

```powershell
git status --short
```

Expected:

```text
```

- [ ] **Step 2: Run current host tests before touching code**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
P4 host tests passed.
```

- [ ] **Step 3: Confirm P4 firmware baseline builds**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

```text
Project build complete.
```

---

## Task 1: Add Pad FX DSP core with tests

**Files:**
- Create: `firmware/main-deck-p4/components/audio_engine/include/audio_pad_fx.h`
- Create: `firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c`
- Create: `tests/audio_pad_fx/test_audio_pad_fx.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Write failing Pad FX core test**

Create `tests/audio_pad_fx/test_audio_pad_fx.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "audio_pad_fx.h"

static void test_pad_fx_defaults_to_bypass(void)
{
    audio_pad_fx_state_t fx;
    audio_pad_fx_init(&fx, 44100u);

    audio_mixer_frame_t in = { .left = 1200, .right = -1200 };
    audio_mixer_frame_t out = audio_pad_fx_process_frame(&fx, in);

    assert(out.left == in.left);
    assert(out.right == in.right);
    assert(!audio_pad_fx_is_active(&fx));
}

static void test_pad_fx_filter_pad_changes_signal(void)
{
    audio_pad_fx_state_t fx;
    audio_pad_fx_init(&fx, 44100u);
    audio_pad_fx_set(&fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX1,
        .pad = 0,
        .active = true,
    });

    audio_mixer_frame_t in = { .left = 16000, .right = -16000 };
    audio_mixer_frame_t out = in;
    for (int i = 0; i < 128; i++) {
        out = audio_pad_fx_process_frame(&fx, in);
    }

    assert(audio_pad_fx_is_active(&fx));
    assert(out.left != in.left || out.right != in.right);
}

static void test_pad_fx_release_clears_active_pad(void)
{
    audio_pad_fx_state_t fx;
    audio_pad_fx_init(&fx, 44100u);
    audio_pad_fx_set(&fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX2,
        .pad = 3,
        .active = true,
    });
    assert(audio_pad_fx_is_active(&fx));

    audio_pad_fx_set(&fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX2,
        .pad = 3,
        .active = false,
    });
    assert(!audio_pad_fx_is_active(&fx));
}

int main(void)
{
    test_pad_fx_defaults_to_bypass();
    test_pad_fx_filter_pad_changes_signal();
    test_pad_fx_release_clears_active_pad();
    puts("audio_pad_fx tests passed");
    return 0;
}
```

- [ ] **Step 2: Add host test entry**

Modify `tests/run_p4_host_tests.ps1` by adding a test item next to `audio_delay_fx`:

```powershell
@{
    Name = "audio_pad_fx"
    Dir = "tests/audio_pad_fx"
    Target = "test_audio_pad_fx.exe"
    Sources = @(
        "test_audio_pad_fx.c",
        "../../firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c",
        "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
        "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c"
    )
    Include = @(
        "../../firmware/main-deck-p4/components/audio_engine/include",
        "../../firmware/main-deck-p4/components/audio_engine"
    )
}
```

If the script uses a different object shape for neighboring tests, use the exact same keys as the `audio_delay_fx` block and only change `Name`, `Dir`, `Target`, and `Sources`.

- [ ] **Step 3: Run test to verify RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: build fails for `audio_pad_fx` because `audio_pad_fx.h` and `audio_pad_fx.c` do not exist.

- [ ] **Step 4: Add Pad FX header**

Create `firmware/main-deck-p4/components/audio_engine/include/audio_pad_fx.h`:

```c
#pragma once

#include "audio_delay_fx.h"
#include "audio_filter.h"
#include "audio_mixer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PAD_FX_MODE_PAD_FX1 = 0,
    AUDIO_PAD_FX_MODE_PAD_FX2 = 1,
} audio_pad_fx_mode_t;

typedef enum {
    AUDIO_PAD_FX_KIND_NONE = 0,
    AUDIO_PAD_FX_KIND_FILTER = 1,
    AUDIO_PAD_FX_KIND_ECHO = 2,
} audio_pad_fx_kind_t;

typedef struct {
    audio_pad_fx_mode_t mode;
    uint8_t pad;
    bool active;
} audio_pad_fx_config_t;

typedef struct {
    audio_filter_state_t filter;
    audio_delay_fx_t echo;
    int16_t *echo_left;
    int16_t *echo_right;
    uint32_t echo_capacity_frames;
    uint32_t sample_rate_hz;
    audio_pad_fx_mode_t mode;
    uint8_t pad;
    audio_pad_fx_kind_t kind;
    bool active;
} audio_pad_fx_state_t;

void audio_pad_fx_init(audio_pad_fx_state_t *fx, uint32_t sample_rate_hz);
bool audio_pad_fx_attach_echo_buffer(audio_pad_fx_state_t *fx,
                                     int16_t *left,
                                     int16_t *right,
                                     uint32_t capacity_frames);
void audio_pad_fx_set(audio_pad_fx_state_t *fx, audio_pad_fx_config_t config);
void audio_pad_fx_clear(audio_pad_fx_state_t *fx);
bool audio_pad_fx_is_active(const audio_pad_fx_state_t *fx);
audio_pad_fx_kind_t audio_pad_fx_kind(const audio_pad_fx_state_t *fx);
audio_mixer_frame_t audio_pad_fx_process_frame(audio_pad_fx_state_t *fx,
                                               audio_mixer_frame_t in);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 5: Add minimal Pad FX implementation**

Create `firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c`:

```c
#include "audio_pad_fx.h"

#define PAD_FX_SAMPLE_RATE_FALLBACK 44100u
#define PAD_FX_ECHO_WET_Q15        14745u
#define PAD_FX_ECHO_FEEDBACK_Q15   9830u

static audio_pad_fx_kind_t kind_for(audio_pad_fx_mode_t mode, uint8_t pad)
{
    (void)mode;
    if (pad == 0 || pad == 1) {
        return AUDIO_PAD_FX_KIND_FILTER;
    }
    if (pad == 2 || pad == 3) {
        return AUDIO_PAD_FX_KIND_ECHO;
    }
    return AUDIO_PAD_FX_KIND_NONE;
}

static uint16_t filter_raw_for(audio_pad_fx_mode_t mode, uint8_t pad)
{
    bool strong = mode == AUDIO_PAD_FX_MODE_PAD_FX2;
    if (pad == 0) {
        return strong ? AUDIO_FILTER_RAW_MIN : (AUDIO_FILTER_RAW_CENTER / 2u);
    }
    if (pad == 1) {
        return strong ? AUDIO_FILTER_RAW_MAX : (AUDIO_FILTER_RAW_CENTER + (AUDIO_FILTER_RAW_CENTER / 2u));
    }
    return AUDIO_FILTER_RAW_CENTER;
}

static uint32_t echo_delay_for(audio_pad_fx_mode_t mode, uint8_t pad)
{
    if (mode == AUDIO_PAD_FX_MODE_PAD_FX2 && pad == 2) return 125u;
    if (mode == AUDIO_PAD_FX_MODE_PAD_FX2 && pad == 3) return 1000u;
    if (pad == 2) return 250u;
    if (pad == 3) return 500u;
    return 0u;
}

void audio_pad_fx_init(audio_pad_fx_state_t *fx, uint32_t sample_rate_hz)
{
    if (!fx) return;
    fx->sample_rate_hz = sample_rate_hz ? sample_rate_hz : PAD_FX_SAMPLE_RATE_FALLBACK;
    fx->echo_left = 0;
    fx->echo_right = 0;
    fx->echo_capacity_frames = 0;
    audio_filter_init(&fx->filter, fx->sample_rate_hz);
    audio_delay_fx_init(&fx->echo, 0, 0, 0, fx->sample_rate_hz);
    audio_pad_fx_clear(fx);
}

bool audio_pad_fx_attach_echo_buffer(audio_pad_fx_state_t *fx,
                                     int16_t *left,
                                     int16_t *right,
                                     uint32_t capacity_frames)
{
    if (!fx || !left || !right || capacity_frames == 0) return false;
    fx->echo_left = left;
    fx->echo_right = right;
    fx->echo_capacity_frames = capacity_frames;
    audio_delay_fx_init(&fx->echo, left, right, capacity_frames, fx->sample_rate_hz);
    return true;
}

void audio_pad_fx_clear(audio_pad_fx_state_t *fx)
{
    if (!fx) return;
    fx->mode = AUDIO_PAD_FX_MODE_PAD_FX1;
    fx->pad = 0;
    fx->kind = AUDIO_PAD_FX_KIND_NONE;
    fx->active = false;
    audio_filter_set_raw(&fx->filter, AUDIO_FILTER_RAW_CENTER);
    audio_filter_reset(&fx->filter);
    audio_delay_fx_reset(&fx->echo);
}

void audio_pad_fx_set(audio_pad_fx_state_t *fx, audio_pad_fx_config_t config)
{
    if (!fx) return;
    if (!config.active) {
        if (fx->active && fx->mode == config.mode && fx->pad == config.pad) {
            audio_pad_fx_clear(fx);
        }
        return;
    }

    fx->mode = config.mode;
    fx->pad = config.pad;
    fx->kind = kind_for(config.mode, config.pad);
    fx->active = fx->kind != AUDIO_PAD_FX_KIND_NONE;

    if (fx->kind == AUDIO_PAD_FX_KIND_FILTER) {
        audio_filter_set_raw(&fx->filter, filter_raw_for(config.mode, config.pad));
        audio_filter_reset(&fx->filter);
    } else if (fx->kind == AUDIO_PAD_FX_KIND_ECHO) {
        audio_delay_fx_configure(&fx->echo, &(audio_delay_fx_config_t) {
            .delay_ms = echo_delay_for(config.mode, config.pad),
            .wet_q15 = PAD_FX_ECHO_WET_Q15,
            .feedback_q15 = PAD_FX_ECHO_FEEDBACK_Q15,
        });
        audio_delay_fx_reset(&fx->echo);
    }
}

bool audio_pad_fx_is_active(const audio_pad_fx_state_t *fx)
{
    return fx && fx->active;
}

audio_pad_fx_kind_t audio_pad_fx_kind(const audio_pad_fx_state_t *fx)
{
    return fx ? fx->kind : AUDIO_PAD_FX_KIND_NONE;
}

audio_mixer_frame_t audio_pad_fx_process_frame(audio_pad_fx_state_t *fx,
                                               audio_mixer_frame_t in)
{
    if (!fx || !fx->active) return in;
    if (fx->kind == AUDIO_PAD_FX_KIND_FILTER) {
        return audio_filter_process_frame(&fx->filter, true, in);
    }
    if (fx->kind == AUDIO_PAD_FX_KIND_ECHO && audio_delay_fx_is_allocated(&fx->echo)) {
        return audio_delay_fx_process_frame(&fx->echo, in);
    }
    return in;
}
```

- [ ] **Step 6: Run Pad FX host test to verify GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
audio_pad_fx tests passed
P4 host tests passed.
```

- [ ] **Step 7: Commit Pad FX core**

Run:

```powershell
git add firmware/main-deck-p4/components/audio_engine/include/audio_pad_fx.h `
        firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c `
        tests/audio_pad_fx/test_audio_pad_fx.c `
        tests/run_p4_host_tests.ps1
git commit -m "feat(audio): add pad fx dsp core"
```

---

## Task 2: Wire Pad FX into audio engine and mixer

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `tests/audio_output_mixer/test_audio_output_mixer.c`
- Modify: `tests/audio_engine/test_audio_engine.c`

- [ ] **Step 1: Add failing mixer test**

Add to `tests/audio_output_mixer/test_audio_output_mixer.c`:

```c
static void test_pad_fx_applies_only_to_target_deck(void)
{
    audio_output_mixer_deck_t deck0 = silent_deck();
    audio_output_mixer_deck_t deck1 = silent_deck();
    int16_t sample0 = 16000;
    int16_t sample1 = 16000;
    deck0.sample = (audio_mixer_frame_t){ .left = sample0, .right = sample0 };
    deck1.sample = (audio_mixer_frame_t){ .left = sample1, .right = sample1 };

    audio_pad_fx_state_t fx0;
    audio_pad_fx_init(&fx0, 44100u);
    audio_pad_fx_set(&fx0, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX1,
        .pad = 0,
        .active = true,
    });

    deck0.pad_fx = &fx0;
    deck0.pad_fx_enabled = true;
    deck1.pad_fx = 0;
    deck1.pad_fx_enabled = false;

    audio_mixer_frame_t out = audio_output_mixer_mix(&deck0, &deck1);

    assert(out.left != sample0 + sample1);
}
```

Call it from `main()` near the Beat FX mixer tests:

```c
test_pad_fx_applies_only_to_target_deck();
```

- [ ] **Step 2: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: build fails because `audio_output_mixer_deck_t` has no `pad_fx` fields.

- [ ] **Step 3: Add mixer fields**

Modify `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`:

```c
#include "audio_pad_fx.h"
```

Add to `audio_output_mixer_deck_t` after existing filter fields:

```c
audio_pad_fx_state_t *pad_fx;
bool pad_fx_enabled;
```

- [ ] **Step 4: Apply Pad FX in mixer**

Modify `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`:

```c
static audio_mixer_frame_t apply_deck_pad_fx(const audio_output_mixer_deck_t *deck,
                                             audio_mixer_frame_t frame)
{
    if (!deck || !deck->pad_fx || !deck->pad_fx_enabled) {
        return frame;
    }
    return audio_pad_fx_process_frame(deck->pad_fx, frame);
}
```

Apply it after deck EQ/filter and before Beat FX filter/Echo:

```c
audio_mixer_frame_t frame0 = apply_deck_beat_fx_echo(deck0,
    apply_deck_beat_fx_filter(deck0,
        apply_deck_pad_fx(deck0,
            apply_deck_filter(deck0,
                apply_deck_eq(deck0, next_deck_frame(deck0, &consumed0))))));
```

Use the same order for deck 1 and for every duplicated mix path in the file.

- [ ] **Step 5: Run mixer GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
audio_output_mixer tests passed
P4 host tests passed.
```

- [ ] **Step 6: Add audio engine Pad FX API test**

Add to `tests/audio_engine/test_audio_engine.c` near existing Beat FX tests:

```c
EXPECT(audio_engine_set_pad_fx(0, AUDIO_ENGINE_PAD_FX_MODE_PAD_FX1, 0, true) == ESP_OK,
       "deck 0 Pad FX1 pad 0 enables");
audio_engine_get_mixer_snapshot(&snapshot);
EXPECT(snapshot.pad_fx_enabled[0], "snapshot captures deck 0 pad fx enabled");
EXPECT(!snapshot.pad_fx_enabled[1], "snapshot leaves deck 1 pad fx disabled");
EXPECT(snapshot.pad_fx_pad[0] == 0, "snapshot captures active pad fx pad");
EXPECT(audio_engine_set_pad_fx(0, AUDIO_ENGINE_PAD_FX_MODE_PAD_FX1, 0, false) == ESP_OK,
       "deck 0 Pad FX release clears");
audio_engine_get_mixer_snapshot(&snapshot);
EXPECT(!snapshot.pad_fx_enabled[0], "snapshot clears deck 0 pad fx");
```

- [ ] **Step 7: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: build fails because `audio_engine_set_pad_fx` and snapshot fields do not exist.

- [ ] **Step 8: Add audio engine API and state**

Modify `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`:

```c
typedef enum {
    AUDIO_ENGINE_PAD_FX_MODE_PAD_FX1 = 0,
    AUDIO_ENGINE_PAD_FX_MODE_PAD_FX2 = 1,
} audio_engine_pad_fx_mode_t;

esp_err_t audio_engine_set_pad_fx(uint8_t deck,
                                  audio_engine_pad_fx_mode_t mode,
                                  uint8_t pad,
                                  bool active);
```

Add to `audio_engine_mixer_snapshot_t`:

```c
bool pad_fx_enabled[AUDIO_ENGINE_DECK_COUNT];
uint8_t pad_fx_pad[AUDIO_ENGINE_DECK_COUNT];
uint8_t pad_fx_mode[AUDIO_ENGINE_DECK_COUNT];
```

Modify `firmware/main-deck-p4/components/audio_engine/audio_engine.c`:

```c
#include "audio_pad_fx.h"
```

Add static state near Beat FX state:

```c
static audio_pad_fx_state_t s_pad_fx[AUDIO_ENGINE_DECK_COUNT];
static int16_t *s_pad_fx_echo_left[AUDIO_ENGINE_DECK_COUNT];
static int16_t *s_pad_fx_echo_right[AUDIO_ENGINE_DECK_COUNT];
```

Initialize in `audio_engine_init()` after filter initialization:

```c
for (uint8_t i = 0; i < AUDIO_ENGINE_DECK_COUNT; i++) {
    audio_pad_fx_init(&s_pad_fx[i], 44100u);
}
```

Allocate echo buffers using the same heap-cap policy as Beat FX Echo; use 1000 ms at 48000 Hz per deck:

```c
#define AUDIO_ENGINE_PAD_FX_ECHO_MAX_DELAY_MS 1000u
#define AUDIO_ENGINE_PAD_FX_ECHO_SAMPLE_RATE  48000u

static uint32_t pad_fx_echo_capacity_frames(void)
{
    return (AUDIO_ENGINE_PAD_FX_ECHO_SAMPLE_RATE * AUDIO_ENGINE_PAD_FX_ECHO_MAX_DELAY_MS) / 1000u;
}
```

Call `audio_pad_fx_attach_echo_buffer()` for each deck after allocation.

Add API:

```c
esp_err_t audio_engine_set_pad_fx(uint8_t deck,
                                  audio_engine_pad_fx_mode_t mode,
                                  uint8_t pad,
                                  bool active)
{
    if (deck >= AUDIO_ENGINE_DECK_COUNT || pad >= 8) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_pad_fx_set(&s_pad_fx[deck], (audio_pad_fx_config_t) {
        .mode = mode == AUDIO_ENGINE_PAD_FX_MODE_PAD_FX2
            ? AUDIO_PAD_FX_MODE_PAD_FX2
            : AUDIO_PAD_FX_MODE_PAD_FX1,
        .pad = pad,
        .active = active,
    });
    return ESP_OK;
}
```

Set mixer descriptors:

```c
.pad_fx = &s_pad_fx[deck0_index],
.pad_fx_enabled = audio_pad_fx_is_active(&s_pad_fx[deck0_index]),
```

Set snapshot fields:

```c
out_snapshot->pad_fx_enabled[deck] = audio_pad_fx_is_active(&s_pad_fx[deck]);
out_snapshot->pad_fx_pad[deck] = s_pad_fx[deck].pad;
out_snapshot->pad_fx_mode[deck] = s_pad_fx[deck].mode;
```

- [ ] **Step 9: Run audio engine GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
audio_engine tests passed
P4 host tests passed.
```

- [ ] **Step 10: Add `audio_pad_fx.c` to firmware CMake**

Modify `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt` and add:

```cmake
"audio_pad_fx.c"
```

to the `SRCS` list.

- [ ] **Step 11: Build firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

```text
Project build complete.
```

- [ ] **Step 12: Commit audio engine wiring**

Run:

```powershell
git add firmware/main-deck-p4/components/audio_engine `
        tests/audio_output_mixer/test_audio_output_mixer.c `
        tests/audio_engine/test_audio_engine.c
git commit -m "feat(audio): route pad fx through mixer"
```

---

## Task 3: Route P4 deck_core Pad FX pad actions

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `tests/deck_core_dual/stubs/audio_engine.h`

- [ ] **Step 1: Extend audio engine stub**

Modify `tests/deck_core_dual/stubs/audio_engine.h`:

```c
extern int audio_engine_stub_pad_fx_deck;
extern int audio_engine_stub_pad_fx_mode;
extern int audio_engine_stub_pad_fx_pad;
extern bool audio_engine_stub_pad_fx_active;
extern int audio_engine_stub_pad_fx_set_count;

typedef enum {
    AUDIO_ENGINE_PAD_FX_MODE_PAD_FX1 = 0,
    AUDIO_ENGINE_PAD_FX_MODE_PAD_FX2 = 1,
} audio_engine_pad_fx_mode_t;

static inline esp_err_t audio_engine_set_pad_fx(uint8_t deck,
                                                audio_engine_pad_fx_mode_t mode,
                                                uint8_t pad,
                                                bool active)
{
    audio_engine_stub_pad_fx_deck = deck;
    audio_engine_stub_pad_fx_mode = (int)mode;
    audio_engine_stub_pad_fx_pad = pad;
    audio_engine_stub_pad_fx_active = active;
    audio_engine_stub_pad_fx_set_count++;
    return ESP_OK;
}
```

Add globals and reset logic in `tests/deck_core_dual/test_deck_core_dual.c`:

```c
int audio_engine_stub_pad_fx_deck;
int audio_engine_stub_pad_fx_mode;
int audio_engine_stub_pad_fx_pad;
bool audio_engine_stub_pad_fx_active;
int audio_engine_stub_pad_fx_set_count;
```

Reset in `reset_audio_engine_stub()`:

```c
audio_engine_stub_pad_fx_deck = -1;
audio_engine_stub_pad_fx_mode = -1;
audio_engine_stub_pad_fx_pad = -1;
audio_engine_stub_pad_fx_active = false;
audio_engine_stub_pad_fx_set_count = 0;
```

- [ ] **Step 2: Add failing deck_core tests**

Add to `tests/deck_core_dual/test_deck_core_dual.c`:

```c
static void test_pad_fx1_pad_action_routes_to_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t mode = deck_button(CTRL_ID_DECK1_PAD_MODE_PAD_FX1);
    deck_core_test_apply_event(&mode);

    ctrl_event_t press = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_DECK1_PAD_ACTION,
        .value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, true),
    };
    deck_core_test_apply_event(&press);

    assert(audio_engine_stub_pad_fx_set_count == 1);
    assert(audio_engine_stub_pad_fx_deck == CTRL_DECK_1);
    assert(audio_engine_stub_pad_fx_mode == AUDIO_ENGINE_PAD_FX_MODE_PAD_FX1);
    assert(audio_engine_stub_pad_fx_pad == 2);
    assert(audio_engine_stub_pad_fx_active);

    ctrl_event_t release = press;
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX1, 2, false, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_pad_fx_set_count == 2);
    assert(!audio_engine_stub_pad_fx_active);
}

static void test_pad_fx2_pad_action_routes_to_audio_engine(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t mode = deck_button(CTRL_ID_DECK2_PAD_MODE_PAD_FX2);
    deck_core_test_apply_event(&mode);

    ctrl_event_t press = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_DECK2_PAD_ACTION,
        .value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_PAD_FX2, 3, true, true),
    };
    deck_core_test_apply_event(&press);

    assert(audio_engine_stub_pad_fx_set_count == 1);
    assert(audio_engine_stub_pad_fx_deck == CTRL_DECK_2);
    assert(audio_engine_stub_pad_fx_mode == AUDIO_ENGINE_PAD_FX_MODE_PAD_FX2);
    assert(audio_engine_stub_pad_fx_pad == 3);
    assert(audio_engine_stub_pad_fx_active);
}
```

Call both tests from `main()`.

- [ ] **Step 3: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: tests fail because `deck_core` logs Pad FX actions as deferred and does not call `audio_engine_set_pad_fx()`.

- [ ] **Step 4: Implement deck_core routing**

Modify `firmware/main-deck-p4/components/deck_core/deck_core.c` inside `CTRL_DECK_CTL_PAD_ACTION` handling before the deferred log branch:

```c
        } else if (CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_PAD_FX1 ||
                   CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_PAD_FX2) {
            audio_engine_pad_fx_mode_t mode =
                CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_PAD_FX2
                    ? AUDIO_ENGINE_PAD_FX_MODE_PAD_FX2
                    : AUDIO_ENGINE_PAD_FX_MODE_PAD_FX1;
            esp_err_t rc = audio_engine_set_pad_fx(deck,
                                                   mode,
                                                   CTRL_PAD_ACTION_PAD(ev->value),
                                                   CTRL_PAD_ACTION_PRESSED(ev->value));
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "deck %u pad fx route failed: %s",
                         (unsigned)deck + 1u,
                         esp_err_to_name(rc));
            }
            publish_flx4_led_snapshot(false);
```

- [ ] **Step 5: Run GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
deck_core_dual tests passed
P4 host tests passed.
```

- [ ] **Step 6: Commit deck_core routing**

Run:

```powershell
git add firmware/main-deck-p4/components/deck_core/deck_core.c `
        tests/deck_core_dual/test_deck_core_dual.c `
        tests/deck_core_dual/stubs/audio_engine.h
git commit -m "feat(deck): route pad fx actions"
```

---

## Task 4: Smart CFX musical curve

**Files:**
- Create: `tests/audio_smart_cfx/test_audio_smart_cfx.c`
- Create: `firmware/main-deck-p4/components/audio_engine/include/audio_smart_cfx.h`
- Create: `firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `tests/audio_engine/test_audio_engine.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Write failing Smart CFX curve test**

Create `tests/audio_smart_cfx/test_audio_smart_cfx.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "audio_smart_cfx.h"
#include "audio_filter.h"

static void test_center_stays_center(void)
{
    assert(audio_smart_cfx_curve_raw(AUDIO_FILTER_RAW_CENTER) == AUDIO_FILTER_RAW_CENTER);
}

static void test_near_center_is_softened(void)
{
    uint16_t input = AUDIO_FILTER_RAW_CENTER - 512u;
    uint16_t curved = audio_smart_cfx_curve_raw(input);
    assert(curved < AUDIO_FILTER_RAW_CENTER);
    assert(curved > input);
}

static void test_extremes_still_reach_extremes(void)
{
    assert(audio_smart_cfx_curve_raw(AUDIO_FILTER_RAW_MIN) == AUDIO_FILTER_RAW_MIN);
    assert(audio_smart_cfx_curve_raw(AUDIO_FILTER_RAW_MAX) == AUDIO_FILTER_RAW_MAX);
}

int main(void)
{
    test_center_stays_center();
    test_near_center_is_softened();
    test_extremes_still_reach_extremes();
    puts("audio_smart_cfx tests passed");
    return 0;
}
```

- [ ] **Step 2: Add host test entry**

Modify `tests/run_p4_host_tests.ps1` and add:

```powershell
@{
    Name = "audio_smart_cfx"
    Dir = "tests/audio_smart_cfx"
    Target = "test_audio_smart_cfx.exe"
    Sources = @(
        "test_audio_smart_cfx.c",
        "../../firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c"
    )
    Include = @(
        "../../firmware/main-deck-p4/components/audio_engine/include"
    )
}
```

- [ ] **Step 3: Run RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: build fails because `audio_smart_cfx.h` and `audio_smart_cfx.c` do not exist.

- [ ] **Step 4: Add Smart CFX curve header**

Create `firmware/main-deck-p4/components/audio_engine/include/audio_smart_cfx.h`:

```c
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t audio_smart_cfx_curve_raw(uint16_t raw);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 5: Add Smart CFX curve implementation**

Create `firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c`:

```c
#include "audio_smart_cfx.h"
#include "audio_filter.h"

uint16_t audio_smart_cfx_curve_raw(uint16_t raw)
{
    if (raw > AUDIO_FILTER_RAW_MAX) raw = AUDIO_FILTER_RAW_MAX;
    if (raw == AUDIO_FILTER_RAW_CENTER ||
        raw == AUDIO_FILTER_RAW_MIN ||
        raw == AUDIO_FILTER_RAW_MAX) {
        return raw;
    }

    int32_t delta = (int32_t)raw - (int32_t)AUDIO_FILTER_RAW_CENTER;
    int32_t sign = delta < 0 ? -1 : 1;
    uint32_t mag = (uint32_t)(delta < 0 ? -delta : delta);
    uint32_t max = AUDIO_FILTER_RAW_CENTER;
    if (mag > max) mag = max;

    uint32_t curved = (mag * mag + (max / 2u)) / max;
    uint32_t min_audible = max / 24u;
    if (mag > min_audible && curved < min_audible) {
        curved = min_audible;
    }

    int32_t out = (int32_t)AUDIO_FILTER_RAW_CENTER + sign * (int32_t)curved;
    if (out < AUDIO_FILTER_RAW_MIN) out = AUDIO_FILTER_RAW_MIN;
    if (out > AUDIO_FILTER_RAW_MAX) out = AUDIO_FILTER_RAW_MAX;
    return (uint16_t)out;
}
```

- [ ] **Step 6: Run Smart CFX GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
audio_smart_cfx tests passed
P4 host tests passed.
```

- [ ] **Step 7: Wire curve into Smart CFX path**

Modify `firmware/main-deck-p4/components/audio_engine/audio_engine.c`:

```c
#include "audio_smart_cfx.h"
```

In `audio_engine_set_filter()` replace:

```c
audio_filter_set_raw(&s_deck_filter[deck], raw_filter);
```

with:

```c
uint16_t effective_filter = s_smart_cfx_enabled
    ? audio_smart_cfx_curve_raw(raw_filter)
    : raw_filter;
audio_filter_set_raw(&s_deck_filter[deck], effective_filter);
```

Keep the snapshot `filter[deck]` as the raw controller value if the engine already tracks raw separately. If it currently only stores the filter state, add:

```c
static uint16_t s_deck_filter_raw[AUDIO_ENGINE_DECK_COUNT];
static uint16_t s_deck_filter_effective[AUDIO_ENGINE_DECK_COUNT];
```

Set both values in `audio_engine_set_filter()` and expose both in snapshot:

```c
out_snapshot->filter[deck] = s_deck_filter_raw[deck];
out_snapshot->smart_cfx_filter_effective[deck] = s_deck_filter_effective[deck];
```

Add to `audio_engine_mixer_snapshot_t`:

```c
uint16_t smart_cfx_filter_effective[AUDIO_ENGINE_DECK_COUNT];
```

- [ ] **Step 8: Add audio engine test for raw/effective split**

Add to `tests/audio_engine/test_audio_engine.c`:

```c
EXPECT(audio_engine_toggle_smart_cfx() == ESP_OK, "Smart CFX can toggle for curve test");
EXPECT(audio_engine_set_filter(0, AUDIO_FILTER_RAW_CENTER - 512u) == ESP_OK,
       "Smart CFX accepts near-center filter raw");
audio_engine_get_mixer_snapshot(&snapshot);
EXPECT(snapshot.filter[0] == AUDIO_FILTER_RAW_CENTER - 512u,
       "snapshot keeps raw Smart CFX filter value");
EXPECT(snapshot.smart_cfx_filter_effective[0] > snapshot.filter[0],
       "Smart CFX softens near-center low-pass travel");
EXPECT(snapshot.smart_cfx_filter_effective[0] < AUDIO_FILTER_RAW_CENTER,
       "Smart CFX remains on low-pass side");
```

- [ ] **Step 9: Run full P4 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected:

```text
audio_smart_cfx tests passed
audio_engine tests passed
P4 host tests passed.
```

- [ ] **Step 10: Add `audio_smart_cfx.c` to firmware CMake**

Modify `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt` and add:

```cmake
"audio_smart_cfx.c"
```

to the `SRCS` list.

- [ ] **Step 11: Build firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

```text
Project build complete.
```

- [ ] **Step 12: Commit Smart CFX refinement**

Run:

```powershell
git add firmware/main-deck-p4/components/audio_engine `
        tests/audio_smart_cfx/test_audio_smart_cfx.c `
        tests/audio_engine/test_audio_engine.c `
        tests/run_p4_host_tests.ps1
git commit -m "feat(audio): refine smart cfx curve"
```

---

## Task 5: Documentation and hardware smoke checklist

**Files:**
- Modify: `README.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`

- [ ] **Step 1: Update Pad FX MIDI map status**

Modify `docs/DDJ_FLX4_MIDI_MAP.md` Pad FX rows:

```markdown
| Pad FX1 mode | `0x90/0x1E`, `0x91/0x1E` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_PAD_FX1`, `CTRL_ID_DECK2_PAD_MODE_PAD_FX1` | P4 pad mode state / Pad FX DSP model | P4 DSP implemented; pad input mapping gated | Mode verified D1/D2 2026-06-21; full Pad FX pad input capture/reconciliation still required |
| Shift + Pad FX1 / Pad FX2 mode | `0x90/0x6B`, `0x91/0x6B` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_PAD_FX2`, `CTRL_ID_DECK2_PAD_MODE_PAD_FX2` | P4 pad mode state / Pad FX DSP model | P4 DSP implemented; pad input mapping gated | Mode verified D1/D2 2026-06-21; full Pad FX2 pad input capture/reconciliation still required |
```

- [ ] **Step 2: Add development status**

Add to `docs/DEVELOPMENT_PLAN.md` near existing Smart CFX/Beat FX status:

```markdown
Pad FX now has a first P4-owned DSP slice behind synthetic `CTRL_PAD_ACTION`
events for PAD_FX1/PAD_FX2, using the existing filter and delay primitives.
Full FLX4 Pad FX pad input mapping remains gated by raw capture or a reconciled
official message list because the XML reference does not expose a complete Pad
FX pad range. Smart CFX now applies a softened macro curve to the deck-local
filter path so near-center movement is gentler while the outer travel still
reaches the current full filter depth.
```

- [ ] **Step 3: Add startup checklist smoke items**

Add to `docs/STARTUP_CHECKLIST.md`:

```markdown
- Pad FX DSP first slice is implemented in P4 and host-tested through synthetic
  `CTRL_PAD_ACTION` events. Hardware input mapping for actual FLX4 Pad FX pads
  remains a separate smoke/capture gate.
- Smart CFX refined curve is implemented and host-tested. Hardware smoke should
  verify: center is neutral, small filter movement is gentle, full left/right
  still reaches audible filter depth, and dual-deck playback remains stable.
```

- [ ] **Step 4: Add README summary**

Add one paragraph to `README.md`:

```markdown
Pad FX has a first P4-owned DSP slice built from the existing filter and delay
primitives, with full hardware pad input mapping still gated by Pad FX pad
address verification. Smart CFX uses a softened macro curve over the deck-local
filter path for more controllable near-center behavior.
```

- [ ] **Step 5: Verify docs**

Run:

```powershell
git diff --check
git status --short
```

Expected: `git diff --check` exits 0. `git status --short` shows only intended source/test/docs files.

- [ ] **Step 6: Commit docs**

Run:

```powershell
git add README.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md docs/DDJ_FLX4_MIDI_MAP.md
git commit -m "docs(fx): record pad fx and smart cfx plan status"
```

---

## Task 6: Firmware verification and hardware smoke

**Files:**
- No source changes expected.

- [ ] **Step 1: Run full host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
.\tests\run_s3_host_tests.ps1
```

Expected:

```text
P4 host tests passed.
S3 host tests passed.
```

- [ ] **Step 2: Build P4 firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

```text
Project build complete.
```

- [ ] **Step 3: Flash P4**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash
```

Expected:

```text
Hash of data verified.
Hard resetting via RTS pin...
Done
```

- [ ] **Step 4: Smart CFX hardware smoke**

Manual checks:

```text
1. Load and play Deck 1.
2. Toggle Smart CFX ON.
3. Move CH1 filter slightly left/right.
4. Confirm near-center movement is gentler than before.
5. Move CH1 filter fully left/right.
6. Confirm full depth is still audible.
7. Repeat on Deck 2.
8. Play both decks and confirm no audio crackle or waveform regression.
```

Expected:

```text
Smart CFX center neutral: PASS
Smart CFX near-center gentle: PASS
Smart CFX full travel audible: PASS
Dual-deck audio stable: PASS
```

- [ ] **Step 5: Pad FX hardware gate**

Manual check:

```text
1. Select PAD FX1 mode on Deck 1.
2. Press performance pads 1-4.
3. Observe whether S3 emits CTRL_ID_DECK1_PAD_ACTION with mode PAD_FX1.
4. Select PAD FX2 mode on Deck 1.
5. Press performance pads 1-4.
6. Observe whether S3 emits CTRL_ID_DECK1_PAD_ACTION with mode PAD_FX2.
7. Repeat Deck 2 only if Deck 1 emits usable events.
```

Expected if existing S3 mapping emits Pad FX actions:

```text
Pad FX1 pad 1-4 trigger P4 DSP: PASS
Pad FX2 pad 1-4 trigger P4 DSP: PASS
Release clears momentary effect: PASS
```

Expected if S3 mapping does not emit Pad FX actions:

```text
P4 Pad FX DSP remains host-tested only.
Create follow-up plan for Pad FX raw MIDI capture/reconciliation.
Do not mark Pad FX hardware smoke complete.
```

- [ ] **Step 6: Final commit and push**

Run:

```powershell
git diff --check
git status --short
git push
```

Expected:

```text
Everything up-to-date
```

or the current branch advances on remote without rejected updates.

---

## Risk controls

- Pad FX Echo uses preallocated buffers only; no allocation in the audio hot path.
- Pad FX defaults to bypass if buffer allocation fails.
- Pad FX never alters Beat FX state.
- Smart CFX curve does not alter Beat FX FILTER raw mapping.
- Pad FX hardware smoke is not considered complete until real S3 Pad FX pad input is verified.
- If waveform or audio stutter appears, disable Pad FX Echo pads first and retest filter-only pads.

---

## Verification summary before marking complete

Required:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
.\tests\run_s3_host_tests.ps1

$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build

git diff --check
```

Hardware smoke required for Smart CFX:

```text
Smart CFX center neutral
Smart CFX near-center gentle
Smart CFX full travel audible
Dual-deck playback stable
```

Hardware smoke required for Pad FX:

```text
Real FLX4 Pad FX pad input reaches P4 as PAD_FX1/PAD_FX2 CTRL_PAD_ACTION
Pads 1-4 produce expected momentary effects
Release clears effect
Dual-deck playback stable
```

