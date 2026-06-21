# FLX4 Beat Jump Behavior Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement P4-owned DDJ-FLX4 Beat Jump playback behavior for shifted cue/loop call buttons and Beat Jump pads.

**Architecture:** Add a small P4 `beat_jump` helper component that calculates target positions from ANLZ beatgrid metadata or BPM fallback. `deck_core` remains the behavior owner for controller events: it obtains per-deck metadata/BPM through weak UI hooks, calls the helper, and seeks only the addressed deck. S3 mapping stays unchanged.

**Tech Stack:** ESP-IDF C components, existing `0xA5` control-link IDs, existing `audio_engine_deck_seek()`, Windows GCC host tests, `tests/run_p4_host_tests.ps1`.

---

## File Structure

- Create `firmware/main-deck-p4/components/beat_jump/CMakeLists.txt`
  - Registers the new P4 helper component.
- Create `firmware/main-deck-p4/components/beat_jump/include/beat_jump.h`
  - Declares the pure target calculation API.
- Create `firmware/main-deck-p4/components/beat_jump/beat_jump.c`
  - Implements beatgrid/BPM fallback target calculation.
- Create `tests/beat_jump/test_beat_jump.c`
  - Host tests for beatgrid path, edge clamping, and BPM fallback.
- Modify `tests/run_p4_host_tests.ps1`
  - Adds the `beat_jump` host test target.
- Modify `firmware/main-deck-p4/components/deck_core/CMakeLists.txt`
  - Adds `beat_jump` and `library` requirements.
- Modify `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Handles shifted cue/loop call Beat Jump buttons and Beat Jump pad actions.
- Modify `firmware/main-deck-p4/components/ui/include/ui.h`
  - Declares a deck metadata getter for `deck_core` weak lookup.
- Modify `firmware/main-deck-p4/components/ui/ui.c`
  - Implements the deck metadata getter from the existing deck ANLZ store.
- Modify `tests/deck_core_dual/test_deck_core_dual.c`
  - Adds TDD coverage for Beat Jump button/pad behavior.
- Modify `tests/deck_core_dual/Makefile` and `tests/run_p4_host_tests.ps1`
  - Adds include/source wiring for `beat_jump` and `rekordbox_anlz.h`.
- Modify docs after implementation:
  - `docs/DDJ_FLX4_MIDI_MAP.md`
  - `docs/DEVELOPMENT_PLAN.md`
  - `docs/STARTUP_CHECKLIST.md`

---

### Task 1: Add Pure Beat Jump Target Helper

**Files:**
- Create: `firmware/main-deck-p4/components/beat_jump/CMakeLists.txt`
- Create: `firmware/main-deck-p4/components/beat_jump/include/beat_jump.h`
- Create: `firmware/main-deck-p4/components/beat_jump/beat_jump.c`
- Create: `tests/beat_jump/test_beat_jump.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Write failing host test for beatgrid calculation**

Create `tests/beat_jump/test_beat_jump.c`:

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "beat_jump.h"
#include "rekordbox_anlz.h"

static void test_uses_nearest_beatgrid_entry(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 3000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 4000, .beat_phase = 3, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 4,
        .bpm = 120,
    };

    assert(beat_jump_calculate_target_ms(2200, 120, 1, &meta) == 3000);
    assert(beat_jump_calculate_target_ms(2800, 120, -1, &meta) == 2000);
}

static void test_clamps_beatgrid_edges(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 3000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 4000, .beat_phase = 3, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 4,
        .bpm = 120,
    };

    assert(beat_jump_calculate_target_ms(900, 120, -32, &meta) == 1000);
    assert(beat_jump_calculate_target_ms(3800, 120, 32, &meta) == 4000);
}

static void test_falls_back_to_bpm_and_clamps_zero(void)
{
    assert(beat_jump_calculate_target_ms(1000, 120, 4, NULL) == 3000);
    assert(beat_jump_calculate_target_ms(1000, 120, -8, NULL) == 0);
    assert(beat_jump_calculate_target_ms(1000, 0, 1, NULL) == 1500);
}

int main(void)
{
    test_uses_nearest_beatgrid_entry();
    test_clamps_beatgrid_edges();
    test_falls_back_to_bpm_and_clamps_zero();
    puts("beat_jump tests passed");
    return 0;
}
```

- [ ] **Step 2: Add the test target to the P4 host runner**

In `tests/run_p4_host_tests.ps1`, add a target before `deck_core_dual`:

```powershell
@{
    Name = "beat_jump"
    Dir = "tests/beat_jump"
    Target = "test_beat_jump.exe"
    Args = @(
        "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
        "-I../../firmware/main-deck-p4/components/beat_jump/include",
        "-I../../firmware/main-deck-p4/components/library/include",
        "-o", "test_beat_jump.exe",
        "test_beat_jump.c",
        "../../firmware/main-deck-p4/components/beat_jump/beat_jump.c"
    )
},
```

- [ ] **Step 3: Run host tests to verify RED**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: fails at `build beat_jump` because `beat_jump.h` or `beat_jump.c` does not exist yet.

- [ ] **Step 4: Add helper header**

Create `firmware/main-deck-p4/components/beat_jump/include/beat_jump.h`:

```c
#pragma once

#include <stdint.h>
#include "rekordbox_anlz.h"

uint32_t beat_jump_calculate_target_ms(uint32_t position_ms,
                                       uint16_t bpm,
                                       int beat_shift,
                                       const anlz_metadata_t *meta);
```

- [ ] **Step 5: Add helper implementation**

Create `firmware/main-deck-p4/components/beat_jump/beat_jump.c`:

```c
#include "beat_jump.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

uint32_t beat_jump_calculate_target_ms(uint32_t position_ms,
                                       uint16_t bpm,
                                       int beat_shift,
                                       const anlz_metadata_t *meta)
{
    if (meta && meta->beats && meta->beat_count > 0) {
        uint16_t closest_idx = 0;
        uint32_t min_diff = UINT32_MAX;
        for (uint16_t i = 0; i < meta->beat_count; i++) {
            uint32_t beat_ms = meta->beats[i].time_ms;
            uint32_t diff = position_ms > beat_ms ? position_ms - beat_ms : beat_ms - position_ms;
            if (diff < min_diff) {
                min_diff = diff;
                closest_idx = i;
            }
        }

        int target_idx = (int)closest_idx + beat_shift;
        if (target_idx < 0) {
            target_idx = 0;
        }
        if (target_idx >= (int)meta->beat_count) {
            target_idx = (int)meta->beat_count - 1;
        }
        return meta->beats[target_idx].time_ms;
    }

    uint16_t safe_bpm = bpm > 0 ? bpm : 120u;
    int64_t beat_len_ms = 60000 / safe_bpm;
    int64_t target_ms = (int64_t)position_ms + (beat_len_ms * (int64_t)beat_shift);
    return target_ms > 0 ? (uint32_t)target_ms : 0u;
}
```

- [ ] **Step 6: Add ESP-IDF component registration**

Create `firmware/main-deck-p4/components/beat_jump/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "beat_jump.c"
    INCLUDE_DIRS "include"
    REQUIRES library
)
```

- [ ] **Step 7: Run helper tests to verify GREEN**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `beat_jump tests passed`; later tasks may still be unchanged.

- [ ] **Step 8: Commit helper**

Run:

```powershell
git add firmware/main-deck-p4/components/beat_jump tests/beat_jump tests/run_p4_host_tests.ps1
git commit -m "feat(p4): add beat jump target helper"
```

---

### Task 2: Add Deck Metadata Hooks for Beat Jump

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/include/ui.h`
- Modify: `firmware/main-deck-p4/components/ui/ui.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add public UI metadata getter prototype**

In `firmware/main-deck-p4/components/ui/include/ui.h`, add after `ui_library_loaded_track_key_for_deck()`:

```c
const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck);
```

Also add the include near the top:

```c
#include "rekordbox_anlz.h"
```

- [ ] **Step 2: Implement getter in UI**

In `firmware/main-deck-p4/components/ui/ui.c`, add near the existing static `ui_deck_anlz()` helper:

```c
const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck)
{
    return ui_deck_anlz(deck);
}
```

- [ ] **Step 3: Add test stubs in deck_core_dual test**

In `tests/deck_core_dual/test_deck_core_dual.c`, include metadata and add globals:

```c
#include "rekordbox_anlz.h"

static const anlz_metadata_t *s_loaded_anlz[DECK_CORE_DECK_COUNT];
static uint16_t s_loaded_bpm[DECK_CORE_DECK_COUNT];
```

Add weak-hook stubs near `ui_library_loaded_track_key_for_deck()`:

```c
const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck)
{
    assert(deck < DECK_CORE_DECK_COUNT);
    return s_loaded_anlz[deck];
}

uint16_t ui_library_deck_bpm(uint8_t deck, uint16_t fallback_bpm)
{
    assert(deck < DECK_CORE_DECK_COUNT);
    return s_loaded_bpm[deck] > 0 ? s_loaded_bpm[deck] : fallback_bpm;
}
```

In `reset_audio_engine_stub()`, reset both arrays:

```c
s_loaded_anlz[deck] = NULL;
s_loaded_bpm[deck] = 0;
```

- [ ] **Step 4: Run P4 host tests**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: pass. This task only adds hooks and stubs, no behavior yet.

- [ ] **Step 5: Commit metadata hooks**

Run:

```powershell
git add firmware/main-deck-p4/components/ui/include/ui.h firmware/main-deck-p4/components/ui/ui.c tests/deck_core_dual/test_deck_core_dual.c
git commit -m "feat(p4): expose deck beat metadata hook"
```

---

### Task 3: Add Failing Deck Core Beat Jump Tests

**Files:**
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `tests/deck_core_dual/Makefile`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Wire beat_jump into deck_core_dual host build**

In `tests/deck_core_dual/Makefile`, add include paths:

```make
           -I../../firmware/main-deck-p4/components/beat_jump/include \
           -I../../firmware/main-deck-p4/components/library/include
```

Add source:

```make
          ../../firmware/main-deck-p4/components/beat_jump/beat_jump.c \
```

In the `deck_core_dual` target args inside `tests/run_p4_host_tests.ps1`, add the same include paths and source:

```powershell
"-I../../firmware/main-deck-p4/components/beat_jump/include",
"-I../../firmware/main-deck-p4/components/library/include",
...
"../../firmware/main-deck-p4/components/beat_jump/beat_jump.c",
```

- [ ] **Step 2: Add helper metadata builders to deck_core_dual test**

In `tests/deck_core_dual/test_deck_core_dual.c`, add above Beat Jump tests:

```c
static anlz_beat_t s_beat_jump_beats[] = {
    {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 2000, .beat_phase = 1, .bpm_x100 = 12000},
    {.time_ms = 3000, .beat_phase = 2, .bpm_x100 = 12000},
    {.time_ms = 4000, .beat_phase = 3, .bpm_x100 = 12000},
    {.time_ms = 5000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 6000, .beat_phase = 1, .bpm_x100 = 12000},
    {.time_ms = 7000, .beat_phase = 2, .bpm_x100 = 12000},
    {.time_ms = 8000, .beat_phase = 3, .bpm_x100 = 12000},
    {.time_ms = 9000, .beat_phase = 0, .bpm_x100 = 12000},
    {.time_ms = 10000, .beat_phase = 1, .bpm_x100 = 12000},
};

static anlz_metadata_t beat_jump_meta(void)
{
    return (anlz_metadata_t) {
        .beats = s_beat_jump_beats,
        .beat_count = (uint16_t)(sizeof(s_beat_jump_beats) / sizeof(s_beat_jump_beats[0])),
        .bpm = 120,
    };
}
```

- [ ] **Step 3: Add failing tests**

Add these tests before `test_smoke_log_policy_rates_limits_deferred_analog_controls()`:

```c
static void test_beat_jump_buttons_seek_by_one_beat_on_requested_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t meta;
    meta = beat_jump_meta();
    s_loaded_anlz[CTRL_DECK_2] = &meta;
    s_loaded_bpm[CTRL_DECK_2] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 4200;
    audio_engine_stub_deck_playing[CTRL_DECK_2] = true;

    ctrl_event_t back = deck_button(CTRL_ID_DECK2_BEAT_JUMP_BACK);
    ctrl_event_t forward = deck_button(CTRL_ID_DECK2_BEAT_JUMP_FORWARD);

    deck_core_test_apply_event(&back);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 3000);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).playing);

    deck_core_test_apply_event(&forward);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_2] == 2);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 4000);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).playing);
}

static void test_beat_jump_pad_maps_pad_index_to_jump_size(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t pad4 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad4.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 3, false, true);
    deck_core_test_apply_event(&pad4);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 18000);

    ctrl_event_t pad5 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad5.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 4, false, true);
    deck_core_test_apply_event(&pad5);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 2);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 20000);
}

static void test_beat_jump_release_event_does_not_seek(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 4, false, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 0);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 20000);
}

static void test_beat_jump_clamps_to_beatgrid_edges(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t meta;
    meta = beat_jump_meta();
    s_loaded_anlz[CTRL_DECK_1] = &meta;
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 1200;

    ctrl_event_t pad1 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad1.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 0, false, true);
    deck_core_test_apply_event(&pad1);

    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 1000);
}
```

Add calls in `main()` after Hot Cue tests:

```c
test_beat_jump_buttons_seek_by_one_beat_on_requested_deck();
test_beat_jump_pad_maps_pad_index_to_jump_size();
test_beat_jump_release_event_does_not_seek();
test_beat_jump_clamps_to_beatgrid_edges();
```

- [ ] **Step 4: Run tests to verify RED**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: fails in `deck_core_dual` because Beat Jump button/pad action currently logs deferred behavior and does not seek.

---

### Task 4: Implement Deck Core Beat Jump Behavior

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/CMakeLists.txt`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`

- [ ] **Step 1: Add dependencies**

In `firmware/main-deck-p4/components/deck_core/CMakeLists.txt`, change:

```cmake
REQUIRES control_link log freertos audio_engine hot_cue_store
```

to:

```cmake
REQUIRES control_link log freertos audio_engine hot_cue_store beat_jump library
```

- [ ] **Step 2: Add includes and weak hooks**

In `deck_core.c`, add includes:

```c
#include "beat_jump.h"
#include "rekordbox_anlz.h"
```

Add weak declarations near existing UI weak hooks:

```c
extern const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck) __attribute__((weak));
extern uint16_t ui_library_deck_bpm(uint8_t deck, uint16_t fallback_bpm) __attribute__((weak));
```

- [ ] **Step 3: Add Beat Jump helper functions**

In `deck_core.c`, near `handle_hot_cue_pad_action()`, add:

```c
static const int s_beat_jump_pad_shifts[8] = {
    -32, -16, -8, -4, 4, 8, 16, 32,
};

static const anlz_metadata_t *loaded_anlz_for_deck(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT || !ui_get_deck_anlz_metadata) {
        return NULL;
    }
    return ui_get_deck_anlz_metadata(deck);
}

static uint16_t loaded_bpm_for_deck(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT || !ui_library_deck_bpm) {
        return 120u;
    }
    return ui_library_deck_bpm(deck, 120u);
}

static void handle_beat_jump(uint8_t deck, int beat_shift, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state || beat_shift == 0) {
        return;
    }

    uint32_t position_ms = current_deck_position_ms(deck, state);
    const anlz_metadata_t *meta = loaded_anlz_for_deck(deck);
    uint16_t bpm = loaded_bpm_for_deck(deck);
    uint32_t target_ms = beat_jump_calculate_target_ms(position_ms, bpm, beat_shift, meta);

    esp_err_t rc = audio_engine_deck_seek(deck, target_ms);
    if (rc == ESP_OK) {
        state->position_ms = target_ms;
        ESP_LOGI(TAG, "deck %u beat jump %+d -> %lu ms",
                 (unsigned)deck + 1,
                 beat_shift,
                 (unsigned long)target_ms);
    } else {
        ESP_LOGW(TAG, "deck %u beat jump %+d failed: %s",
                 (unsigned)deck + 1,
                 beat_shift,
                 esp_err_to_name(rc));
    }
}

static bool beat_jump_shift_for_pad(uint8_t pad, int *out_shift)
{
    if (!out_shift || pad >= 8) {
        return false;
    }
    *out_shift = s_beat_jump_pad_shifts[pad];
    return true;
}
```

- [ ] **Step 4: Replace deferred Beat Jump button handling**

Replace the `CTRL_DECK_CTL_BEAT_JUMP_BACK/FORWARD` case in `deck_core.c` with:

```c
case CTRL_DECK_CTL_BEAT_JUMP_BACK:
case CTRL_DECK_CTL_BEAT_JUMP_FORWARD:
    if (pressed) {
        handle_beat_jump(deck,
                         control_link_id_control(ev->id) == CTRL_DECK_CTL_BEAT_JUMP_BACK ? -1 : 1,
                         state);
    }
    return true;
```

- [ ] **Step 5: Extend pad action handling**

Replace the `CTRL_DECK_CTL_PAD_ACTION` case with:

```c
case CTRL_DECK_CTL_PAD_ACTION:
    if (CTRL_PAD_ACTION_PRESSED(ev->value) &&
        CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_HOT_CUE) {
        handle_hot_cue_pad_action(deck,
                                  CTRL_PAD_ACTION_PAD(ev->value),
                                  CTRL_PAD_ACTION_SHIFTED(ev->value),
                                  state);
    } else if (CTRL_PAD_ACTION_PRESSED(ev->value) &&
               CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_BEAT_JUMP &&
               !CTRL_PAD_ACTION_SHIFTED(ev->value)) {
        int beat_shift = 0;
        if (beat_jump_shift_for_pad(CTRL_PAD_ACTION_PAD(ev->value), &beat_shift)) {
            handle_beat_jump(deck, beat_shift, state);
        }
    } else if (should_log_deferred_button(ev->id, ev->value)) {
        ESP_LOGI(TAG, "deck %u pad action mode=%u pad=%u shifted=%u (behavior deferred)",
                 (unsigned)deck + 1,
                 (unsigned)CTRL_PAD_ACTION_MODE(ev->value),
                 (unsigned)CTRL_PAD_ACTION_PAD(ev->value),
                 CTRL_PAD_ACTION_SHIFTED(ev->value) ? 1u : 0u);
    }
    return true;
```

- [ ] **Step 6: Run P4 host tests to verify GREEN**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `deck_core_dual tests passed` and final `P4 host tests passed`.

- [ ] **Step 7: Commit Beat Jump behavior**

Run:

```powershell
git add firmware/main-deck-p4/components/deck_core/CMakeLists.txt firmware/main-deck-p4/components/deck_core/deck_core.c tests/deck_core_dual/Makefile tests/deck_core_dual/test_deck_core_dual.c tests/run_p4_host_tests.ps1
git commit -m "feat(deck): implement flx4 beat jump behavior"
```

---

### Task 5: Build, Documentation, and Hardware Handoff

**Files:**
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Build P4 firmware**

Run:

```powershell
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: `Project build complete.`

- [ ] **Step 2: Update MIDI map status**

In `docs/DDJ_FLX4_MIDI_MAP.md`, change the Beat Jump button rows:

```markdown
| Cue/Loop Call Left + Shift / jump back | `0x90/0x3E`, `0x91/0x3E` | press/release | shifted deck-local | `CTRL_ID_DECK1_BEAT_JUMP_BACK`, `CTRL_ID_DECK2_BEAT_JUMP_BACK` | `deck_core` beat jump | Implemented | Verified 2026-06-21; behavior smoke pending |
| Cue/Loop Call Right + Shift / jump forward | `0x90/0x3D`, `0x91/0x3D` | press/release | shifted deck-local | `CTRL_ID_DECK1_BEAT_JUMP_FORWARD`, `CTRL_ID_DECK2_BEAT_JUMP_FORWARD` | `deck_core` beat jump | Implemented | Verified 2026-06-21; behavior smoke pending |
```

Change the Beat Jump pad row:

```markdown
| Beat Jump pads 1-8 | D1 `0x97/0x20..0x27`, D2 `0x99/0x20..0x27` | press/release | active Beat Jump mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | `deck_core` beat jump | Implemented | Verified D1 pads 1-8 and D2 pads 1,3-8 2026-06-21; behavior smoke pending |
```

Keep shifted Beat Jump size pads deferred:

```markdown
| Beat Jump shifted size pads | D1 `0x98/0x26..0x27`, D2 `0x9A/0x26..0x27` | press/release | shifted Beat Jump mode | pad action with size inc/dec | future beat jump size state | Deferred | Not captured |
```

- [ ] **Step 3: Update development plan and startup checklist**

In `docs/DEVELOPMENT_PLAN.md`, replace the Beat Jump deferred sentence in Phase 7 with:

```markdown
P4 loop behavior is implemented for Loop In/Out, Reloop/Exit, and halve/double.
Beat Jump behavior is implemented for shifted cue/loop call buttons and Beat
Jump pads using beatgrid/BPM target calculation. Beat Sync and tempo-range
behavior remain deferred.
```

In `docs/STARTUP_CHECKLIST.md`, update the controller expansion section to state:

```markdown
Loop In/Out, Reloop/Exit, loop halve/double, and Beat Jump buttons/pads now
have P4 behavior; Beat Jump hardware behavior smoke remains pending.
```

- [ ] **Step 4: Verify docs**

Run:

```powershell
git diff --check
git status --short
```

Expected: `git diff --check` exit 0. `git status --short` shows only intended docs and code changes if not already committed.

- [ ] **Step 5: Commit docs**

Run:

```powershell
git add docs/DDJ_FLX4_MIDI_MAP.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md
git commit -m "docs: record flx4 beat jump behavior"
```

- [ ] **Step 6: Push branch**

Run:

```powershell
git push -u origin codex/phase7-extended-controls-vu
```

Expected: push succeeds.

- [ ] **Step 7: Hardware smoke handoff**

Flash P4 only unless S3 or shared protocol changed:

```powershell
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash
idf.py -p COM15 monitor
```

Smoke sequence:

1. Load a beatgrid-backed track on Deck 1.
2. Press Beat Jump mode.
3. Press pad 4: expect a short backward jump.
4. Press pad 5: expect a short forward jump.
5. Press pad 1 near track start: expect clamp to first beat, no crash.
6. While playing, press pad 5: expect playback continues after seek.
7. If time allows, repeat pad 4/5 on Deck 2.

Record pass/fail in docs after hardware smoke.

---

## Self-Review Notes

Spec coverage:

- Shifted cue/loop call buttons: Task 3 and Task 4.
- Beat Jump pads 1-8: Task 3 and Task 4.
- Beatgrid/BPM fallback: Task 1 helper tests and Task 4 integration.
- Deck-local behavior: Task 3 tests.
- Release no-op: Task 3 test.
- Hardware smoke: Task 5.

Known scope exclusions are explicit: Beat Sync, master tempo/key lock, shifted size inc/dec, Beat Jump LEDs, and Mixxx JavaScript behavior.
