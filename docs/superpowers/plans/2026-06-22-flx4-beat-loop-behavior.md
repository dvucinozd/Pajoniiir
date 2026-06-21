# FLX4 Beat Loop Behavior Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement P4-owned DDJ-FLX4 Beat Loop pad behavior for already-mapped pad action events.

**Architecture:** Add a small pure loop-duration helper in the existing P4 `beat_jump` component because it already owns beatgrid/BPM target math. `deck_core` remains the behavior owner: it receives Beat Loop pad actions, reads current deck position and metadata through existing hooks, and calls the per-deck audio loop API. S3 and control-link mappings stay unchanged.

**Tech Stack:** ESP-IDF C components, existing `0xA5` control-link IDs, existing `audio_engine_deck_set_loop()`, Windows GCC host tests, `tests/run_p4_host_tests.ps1`.

---

## File Structure

- Modify `firmware/main-deck-p4/components/beat_jump/include/beat_jump.h`
  - Add a pure API for calculating Beat Loop duration in milliseconds.
- Modify `firmware/main-deck-p4/components/beat_jump/beat_jump.c`
  - Implement beatgrid/BPM loop-duration calculation.
- Modify `tests/beat_jump/test_beat_jump.c`
  - Add helper tests for Beat Loop duration.
- Modify `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Handle `CTRL_PAD_MODE_BEAT_LOOP` pad actions.
- Modify `tests/deck_core_dual/test_deck_core_dual.c`
  - Add failing tests for deck-local Beat Loop behavior.
- Modify docs after implementation:
  - `docs/DDJ_FLX4_MIDI_MAP.md`
  - `docs/DEVELOPMENT_PLAN.md`
  - `docs/STARTUP_CHECKLIST.md`

---

### Task 1: Add Pure Beat Loop Duration Helper

**Files:**
- Modify: `firmware/main-deck-p4/components/beat_jump/include/beat_jump.h`
- Modify: `firmware/main-deck-p4/components/beat_jump/beat_jump.c`
- Modify: `tests/beat_jump/test_beat_jump.c`

- [ ] **Step 1: Write failing helper tests**

Add tests to `tests/beat_jump/test_beat_jump.c`:

```c
static void test_loop_duration_uses_local_beatgrid_spacing(void)
{
    anlz_beat_t beats[] = {
        {.time_ms = 1000, .beat_phase = 0, .bpm_x100 = 12000},
        {.time_ms = 1500, .beat_phase = 1, .bpm_x100 = 12000},
        {.time_ms = 2000, .beat_phase = 2, .bpm_x100 = 12000},
        {.time_ms = 2500, .beat_phase = 3, .bpm_x100 = 12000},
    };
    anlz_metadata_t meta = {
        .beats = beats,
        .beat_count = 4,
        .bpm = 120,
    };

    assert(beat_loop_calculate_duration_ms(1750, 120, 1, 1, &meta) == 500);
    assert(beat_loop_calculate_duration_ms(1750, 120, 4, 1, &meta) == 2000);
}

static void test_loop_duration_supports_fractional_lengths(void)
{
    assert(beat_loop_calculate_duration_ms(1000, 120, 1, 2, NULL) == 250);
    assert(beat_loop_calculate_duration_ms(1000, 120, 1, 4, NULL) == 125);
    assert(beat_loop_calculate_duration_ms(1000, 120, 1, 32, NULL) == 16);
}

static void test_loop_duration_falls_back_to_default_bpm(void)
{
    assert(beat_loop_calculate_duration_ms(1000, 0, 1, 1, NULL) == 500);
    assert(beat_loop_calculate_duration_ms(1000, 0, 2, 1, NULL) == 1000);
}
```

Call them from `main()`.

- [ ] **Step 2: Run host tests to verify RED**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `build beat_jump` fails because `beat_loop_calculate_duration_ms()` is not declared.

- [ ] **Step 3: Add helper declaration**

Add to `firmware/main-deck-p4/components/beat_jump/include/beat_jump.h`:

```c
uint32_t beat_loop_calculate_duration_ms(uint32_t position_ms,
                                         uint16_t bpm,
                                         uint16_t beat_numerator,
                                         uint16_t beat_denominator,
                                         const anlz_metadata_t *meta);
```

- [ ] **Step 4: Implement helper**

Add to `firmware/main-deck-p4/components/beat_jump/beat_jump.c`:

```c
uint32_t beat_loop_calculate_duration_ms(uint32_t position_ms,
                                         uint16_t bpm,
                                         uint16_t beat_numerator,
                                         uint16_t beat_denominator,
                                         const anlz_metadata_t *meta)
{
    uint32_t beat_len_ms = 0;
    if (meta && meta->beats && meta->beat_count >= 2) {
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

        if (closest_idx + 1u < meta->beat_count) {
            beat_len_ms = meta->beats[closest_idx + 1u].time_ms - meta->beats[closest_idx].time_ms;
        } else {
            beat_len_ms = meta->beats[closest_idx].time_ms - meta->beats[closest_idx - 1u].time_ms;
        }
    }

    if (beat_len_ms == 0) {
        uint16_t safe_bpm = bpm > 0 ? bpm : 120u;
        beat_len_ms = 60000u / safe_bpm;
    }

    uint32_t numerator = beat_numerator > 0 ? beat_numerator : 1u;
    uint32_t denominator = beat_denominator > 0 ? beat_denominator : 1u;
    uint64_t duration = ((uint64_t)beat_len_ms * numerator + denominator - 1u) / denominator;
    return duration > 0 ? (uint32_t)duration : 1u;
}
```

- [ ] **Step 5: Run host tests to verify GREEN**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `beat_jump tests passed` and full P4 host suite passes.

- [ ] **Step 6: Commit helper**

```powershell
git add firmware/main-deck-p4/components/beat_jump tests/beat_jump
git commit -m "feat(p4): add beat loop duration helper"
```

---

### Task 2: Add Failing Deck Core Beat Loop Tests

**Files:**
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add Beat Loop tests**

Add tests near existing Beat Jump tests:

```c
static void test_beat_loop_pad_sets_loop_on_requested_deck(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_2] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 10000;

    ctrl_event_t pad6 = deck_button(CTRL_ID_DECK2_PAD_ACTION);
    pad6.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 5, false, true);
    deck_core_test_apply_event(&pad6);

    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_active[CTRL_DECK_2]);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 10000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 10500);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_2] == 1);
}

static void test_beat_loop_pad_maps_pad_index_to_loop_length(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t pad5 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad5.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 4, false, true);
    deck_core_test_apply_event(&pad5);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 20000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 20250);

    ctrl_event_t pad8 = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    pad8.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 7, false, true);
    deck_core_test_apply_event(&pad8);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 20000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 22000);
    assert(audio_engine_stub_loop_set_count[CTRL_DECK_1] == 2);
}

static void test_beat_loop_release_event_does_not_set_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 120;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 20000;

    ctrl_event_t release = deck_button(CTRL_ID_DECK1_PAD_ACTION);
    release.value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 5, false, false);
    deck_core_test_apply_event(&release);

    assert(audio_engine_stub_loop_set_count[CTRL_DECK_1] == 0);
    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
}
```

Call them from `main()` after existing loop tests.

- [ ] **Step 2: Run host tests to verify RED**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `deck_core_dual` fails because Beat Loop pad behavior is currently deferred.

---

### Task 3: Implement Deck Core Beat Loop Behavior

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`

- [ ] **Step 1: Add pad length table and handler**

Add near Beat Jump helper functions:

```c
typedef struct {
    uint16_t numerator;
    uint16_t denominator;
} beat_loop_length_t;

static const beat_loop_length_t s_beat_loop_pad_lengths[8] = {
    {1, 32}, {1, 16}, {1, 8}, {1, 4},
    {1, 2}, {1, 1}, {2, 1}, {4, 1},
};

static bool beat_loop_length_for_pad(uint8_t pad, beat_loop_length_t *out_length)
{
    if (!out_length || pad >= 8) {
        return false;
    }
    *out_length = s_beat_loop_pad_lengths[pad];
    return true;
}

static void handle_beat_loop_pad_action(uint8_t deck, uint8_t pad, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    beat_loop_length_t length = {0};
    if (!beat_loop_length_for_pad(pad, &length)) {
        return;
    }

    uint32_t start_ms = current_deck_position_ms(deck, state);
    uint32_t duration_ms = beat_loop_calculate_duration_ms(start_ms,
                                                           loaded_bpm_for_deck(deck),
                                                           length.numerator,
                                                           length.denominator,
                                                           loaded_anlz_for_deck(deck));
    if (duration_ms == 0 || start_ms > UINT32_MAX - duration_ms) {
        return;
    }
    set_deck_loop(deck, start_ms, start_ms + duration_ms);
}
```

- [ ] **Step 2: Extend pad action handling**

In the `CTRL_DECK_CTL_PAD_ACTION` case, add a branch before deferred logging:

```c
        } else if (CTRL_PAD_ACTION_PRESSED(ev->value) &&
                   CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_BEAT_LOOP &&
                   !CTRL_PAD_ACTION_SHIFTED(ev->value)) {
            handle_beat_loop_pad_action(deck, CTRL_PAD_ACTION_PAD(ev->value), state);
```

- [ ] **Step 3: Run host tests to verify GREEN**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `deck_core_dual tests passed` and final `P4 host tests passed`.

- [ ] **Step 4: Commit behavior**

```powershell
git add firmware/main-deck-p4/components/deck_core/deck_core.c tests/deck_core_dual/test_deck_core_dual.c
git commit -m "feat(deck): implement flx4 beat loop pads"
```

---

### Task 4: Build, Documentation, and Push

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

- [ ] **Step 2: Update docs**

Update `docs/DDJ_FLX4_MIDI_MAP.md` Beat Loop pads row to:

```markdown
| Beat Loop pads 1-8 | D1 `0x97/0x60..0x67`, D2 `0x99/0x60..0x67` | press/release | active Beat Loop mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | `deck_core` beat loop | Implemented | Verified D1/D2 pads 1-8 2026-06-21; behavior smoke pending |
```

Update `docs/DEVELOPMENT_PLAN.md` and `docs/STARTUP_CHECKLIST.md` to state that Beat Loop pad behavior is implemented in P4 and hardware behavior smoke remains pending.

- [ ] **Step 3: Verify docs and status**

Run:

```powershell
git diff --check
git status --short
```

Expected: `git diff --check` exit 0 and only intended docs are modified.

- [ ] **Step 4: Commit docs**

```powershell
git add docs/DDJ_FLX4_MIDI_MAP.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md
git commit -m "docs: record flx4 beat loop behavior"
```

- [ ] **Step 5: Push branch**

```powershell
git push -u origin codex/phase7-extended-controls-vu
```

Expected: push succeeds.

---

## Self-Review Notes

- Beat Loop pad behavior is deck-local and P4-owned.
- S3 mapping and control-link protocol stay unchanged.
- Release events are covered as no-op.
- Shifted Beat Loop pads and Beat Loop pad LEDs stay out of scope.
- Hardware behavior smoke remains pending after build and push.
