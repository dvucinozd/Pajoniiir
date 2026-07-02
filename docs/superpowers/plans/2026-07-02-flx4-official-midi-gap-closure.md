# FLX4 Official MIDI Gap Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the seven DDJ-FLX4 gaps found by comparing the official `DDJ-FLX4_MIDI_message_List_E1.pdf` against the current S3/P4 firmware, excluding Key Shift, Stem, and Keyboard behavior.

**Architecture:** Keep the S3 as the MIDI-to-semantic mapper and keep the P4 authoritative for state, audio, playback, and LED decisions. Extend the existing `0xA5` `control_link` IDs only where a new semantic control is required, and preserve existing IDs. Treat Key Shift, Stem, and Keyboard pad behavior as explicitly out of scope; existing mode mapping may remain, but no new behavior should be added for those modes.

**Tech Stack:** ESP-IDF C firmware, existing S3 `flx4_midi_host`, existing shared `control_link` headers, P4 `deck_core`, P4 `audio_engine`, host tests under `tests/`, ESP-IDF v5.5 builds for both firmware targets.

---

## Scope Rules

- Implement or explicitly ignore the seven identified gaps:
  - Master Cue + Shift mapping bug.
  - Shift + Load semantic mapping.
  - Headphone Level 14-bit input.
  - Shift + Smart CFX / Shift + Smart Fader.
  - Shifted Beat FX beat buttons.
  - Fader start MIDI messages.
  - LED/output candidates.
- Do not implement Key Shift behavior.
- Do not implement Stem behavior.
- Do not implement Keyboard behavior.
- Do not add a new physical raw MIDI capture prerequisite; the official PDF and existing Mixxx XML are sufficient sources for addresses.
- Keep generated ESP-IDF outputs, `sdkconfig`, `dependencies.lock`, and temporary files untracked.

## File Map

- `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
  - Owns DDJ-FLX4 MIDI status/data mapping to semantic `control_link` events.
  - Add official PDF constants and map new controls.
  - Fix Master Cue + Shift from `0x68` to `0x78`.
  - Explicitly ignore fader-start pseudo-events unless a later product decision changes.

- `firmware/control-board-s3/components/flx4_midi_host/include/flx4_map.h`
  - Owns S3 mapping state.
  - Add 14-bit state for Headphone Level.

- `firmware/control-board-s3/components/control_link/include/control_link.h`
  - S3 copy of shared semantic IDs and LED IDs.
  - Add IDs for Headphone Level, Shift + Load, shifted Smart controls, shifted Beat FX beat controls, and new LED candidates.

- `firmware/main-deck-p4/components/control_link/include/control_link.h`
  - P4 copy of shared semantic IDs and LED IDs.
  - Must match S3 constants exactly.

- `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Owns semantic behavior and LED state.
  - Route Shift + Load, shifted Smart controls, shifted Beat FX beat controls, Headphone Level, and candidate LEDs.

- `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
  - Add a public setter/getter for headphone output level if no equivalent exists.

- `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
  - Store and apply Headphone Level to monitor/headphone output gain.

- `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
  - Apply headphone scalar if the existing monitor path requires this layer to consume it.

- `firmware/control-board-s3/components/control_link/flx4_led_midi.c`
  - Map new LED IDs to official MIDI output notes/statuses.

- `firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c`
  - Include new LED IDs in reconnect snapshots where the state is P4-owned.

- `docs/DDJ_FLX4_MIDI_MAP.md`
  - Correct Master Cue + Shift address and document implemented/ignored gaps.

- `docs/CONTROL_LINK_PROTOCOL.md`
  - Document new semantic IDs and LED IDs.

- `docs/DEVELOPMENT_PLAN.md`
  - Record this closure phase and explicit exclusions.

- `docs/STARTUP_CHECKLIST.md`
  - Add acceptance checks for hardware smoke.

- `tests/flx4_midi_host/test_flx4_map.c`
  - S3 mapping tests for all new official-PDF inputs.

- `tests/flx4_midi_host/test_flx4_led_midi.c`
  - LED MIDI packet tests for new output candidates.

- `tests/control_link_protocol/test_control_link_protocol.c`
  - Shared S3/P4 constant parity.

- `tests/control_link_protocol/s3_constants.c`
  - Expose new S3 constants to parity test.

- `tests/control_link_protocol/p4_constants.c`
  - Expose new P4 constants to parity test.

- `tests/deck_core_dual/test_deck_core_dual.c`
  - P4 behavior tests for new semantic events and LED updates.

- `tests/audio_engine/test_audio_engine.c`
  - Headphone Level setter/getter and output behavior tests.

---

## Shared ID Allocation

Use these IDs unless a conflict is discovered during implementation:

```c
/* control_link.h */
#define CTRL_ID_SHIFT_LOAD_DECK1       (CTRL_NS_BROWSER | 0x06)
#define CTRL_ID_SHIFT_LOAD_DECK2       (CTRL_NS_BROWSER | 0x07)

#define CTRL_ID_HEADPHONE_LEVEL        0x7D
#define CTRL_ID_SMART_CFX_SHIFT        0x7E
#define CTRL_ID_SMART_FADER_SHIFT      0x7F
#define CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT 0x83
#define CTRL_ID_BEAT_FX_BEAT_INC_SHIFT 0x84

#define LED_CENSOR                     53
#define LED_CUE_SHIFT                  54
#define LED_LOOP_ADJUST_IN             55
#define LED_LOOP_ADJUST_OUT            56
#define LED_TRACK_LOAD_DECK1           57
#define LED_TRACK_LOAD_DECK2           58
#define LED_BEAT_JUMP_PAD_1            59
#define LED_BEAT_JUMP_PAD_2            60
#define LED_BEAT_JUMP_PAD_3            61
#define LED_BEAT_JUMP_PAD_4            62
#define LED_BEAT_JUMP_PAD_5            63
#define LED_BEAT_JUMP_PAD_6            64
#define LED_BEAT_JUMP_PAD_7            65
#define LED_BEAT_JUMP_PAD_8            66
#define LED_SAMPLER_PAD_1              67
#define LED_SAMPLER_PAD_2              68
#define LED_SAMPLER_PAD_3              69
#define LED_SAMPLER_PAD_4              70
#define LED_SAMPLER_PAD_5              71
#define LED_SAMPLER_PAD_6              72
#define LED_SAMPLER_PAD_7              73
#define LED_SAMPLER_PAD_8              74
#define LED_REMOTE_COUNT               75
```

The new global IDs after `CTRL_ID_MASTER_CUE` are allocated as literal byte IDs because `CTRL_NS_SYSTEM | offset` aliases existing IDs when `offset` is above `0x0F` (`0x70 | 0x10 == 0x70`). `CTRL_ID_HEADPHONE_LEVEL` still travels as `CTRL_TYPE_PITCH`; P4 routes the ID to monitor/headphone gain instead of system-button behavior.

---

### Task 1: Fix Master Cue + Shift Mapping

**Files:**
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`

- [ ] **Step 1: Update failing S3 mapping test**

In `tests/flx4_midi_host/test_flx4_map.c`, update `test_jog_search_and_master_cue_controls()` so Master Cue + Shift uses the official PDF address `0x96/0x78`, and `0x96/0x68` is no longer accepted as Master Cue:

```c
    assert(flx4_map_message(&state, MSG(0x96, 0x63, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x63, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x78, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_MASTER_CUE, 1);
```

Do not add an assertion that `0x96/0x68` is unsupported in this task because Task 2 will map it to Shift + Load Deck 1.

- [ ] **Step 2: Run S3 host map test and verify failure**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
```

Expected before implementation: `test_flx4_map` fails because `0x96/0x78` is not mapped to `CTRL_ID_MASTER_CUE`.

- [ ] **Step 3: Correct the constant**

In `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`, replace:

```c
#define FLX4_BTN_MASTER_CUE_SHIFT 0x68
```

with:

```c
#define FLX4_BTN_MASTER_CUE_SHIFT 0x78
```

- [ ] **Step 4: Update documentation**

In `docs/DDJ_FLX4_MIDI_MAP.md`, update the Master Cue row:

```markdown
| Master Cue | official MIDI list: normal `0x96/0x63`, shifted `0x96/0x78`; LED output `0x96/0x63` | press/release; press toggles, release ignored | global monitor | `CTRL_ID_MASTER_CUE` / `LED_MASTER_CUE` | P4 monitor master-cue gate + reconnect-safe LED snapshot | Implemented | Host-tested from official MIDI list; hardware smoke passed 2026-07-02; shifted address corrected from `0x68` to `0x78` from official PDF |
```

- [ ] **Step 5: Verify**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
git diff --check
```

Expected: S3 host tests pass; `git diff --check` has no output.

- [ ] **Step 6: Commit**

```powershell
git add firmware/control-board-s3/components/flx4_midi_host/flx4_map.c tests/flx4_midi_host/test_flx4_map.c docs/DDJ_FLX4_MIDI_MAP.md
git commit -m "fix: correct flx4 master cue shift mapping"
```

---

### Task 2: Add Shift + Load Semantic Mapping

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`

**Behavior decision:** Shift + Load is mapped, but P4 implements it conservatively as "load selected library track to the same deck and mark it as shifted-load in logs/state only if future behavior needs it." If existing `CTRL_ID_LOAD_DECK*` already loads selected track, this task should make Shift + Load call the same P4 load path through distinct IDs. That preserves address correctness without inventing a destructive alternate load behavior.

- [ ] **Step 1: Add failing control_link parity declarations**

In `tests/control_link_protocol/s3_constants.c`, add:

```c
int s3_ctrl_id_shift_load_deck1(void) { return CTRL_ID_SHIFT_LOAD_DECK1; }
int s3_ctrl_id_shift_load_deck2(void) { return CTRL_ID_SHIFT_LOAD_DECK2; }
```

In `tests/control_link_protocol/p4_constants.c`, add:

```c
int p4_ctrl_id_shift_load_deck1(void) { return CTRL_ID_SHIFT_LOAD_DECK1; }
int p4_ctrl_id_shift_load_deck2(void) { return CTRL_ID_SHIFT_LOAD_DECK2; }
```

In `tests/control_link_protocol/test_control_link_protocol.c`, add prototypes near the other browser/system constants:

```c
int s3_ctrl_id_shift_load_deck1(void);
int s3_ctrl_id_shift_load_deck2(void);
int p4_ctrl_id_shift_load_deck1(void);
int p4_ctrl_id_shift_load_deck2(void);
```

Then add assertions near the `CTRL_ID_LOAD_DECK*` assertions:

```c
    assert(s3_ctrl_id_shift_load_deck1() == p4_ctrl_id_shift_load_deck1());
    assert(s3_ctrl_id_shift_load_deck2() == p4_ctrl_id_shift_load_deck2());
    assert(s3_ctrl_id_shift_load_deck1() == CTRL_ID_SHIFT_LOAD_DECK1);
    assert(s3_ctrl_id_shift_load_deck2() == CTRL_ID_SHIFT_LOAD_DECK2);
```

- [ ] **Step 2: Run protocol test and verify compile failure**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
```

Expected before implementation: compile failure for missing `CTRL_ID_SHIFT_LOAD_DECK1` and `CTRL_ID_SHIFT_LOAD_DECK2`.

- [ ] **Step 3: Add shared IDs**

Add to both S3 and P4 `control_link.h`, after `CTRL_ID_BROWSE_SHIFT_PRESS`:

```c
#define CTRL_ID_SHIFT_LOAD_DECK1 (CTRL_NS_BROWSER | 0x06)
#define CTRL_ID_SHIFT_LOAD_DECK2 (CTRL_NS_BROWSER | 0x07)
```

- [ ] **Step 4: Add failing S3 mapping test**

In `tests/flx4_midi_host/test_flx4_map.c`, extend `test_transport_load_and_pfl_buttons()`:

```c
    assert(flx4_map_message(&state, MSG(0x96, 0x68, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK1, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x68, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK1, 0);
    assert(flx4_map_message(&state, MSG(0x96, 0x7A, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SHIFT_LOAD_DECK2, 1);
```

- [ ] **Step 5: Implement S3 mapping**

In `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`, add constants:

```c
#define FLX4_BTN_LOAD_D1_SHIFT 0x68
#define FLX4_BTN_LOAD_D2_SHIFT 0x7A
```

In the `FLX4_STATUS_GLOBAL_BTN` branch, after normal load mapping:

```c
        if (msg->data1 == FLX4_BTN_LOAD_D1_SHIFT) {
            return emit_button(out, CTRL_ID_SHIFT_LOAD_DECK1, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_LOAD_D2_SHIFT) {
            return emit_button(out, CTRL_ID_SHIFT_LOAD_DECK2, msg->data2 > 0 ? 1 : 0);
        }
```

- [ ] **Step 6: Add P4 behavior test**

In `tests/deck_core_dual/test_deck_core_dual.c`, add a test beside existing library load tests. Use the existing load stubs already present in the file. The assertion should verify that shifted load routes to the same selected-track load path as normal load:

```c
static void test_shift_load_routes_to_same_load_path(void)
{
    deck_core_test_reset();

    ctrl_event_t load1 = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_SHIFT_LOAD_DECK1,
        .value = 1,
        .deck = CTRL_DECK_NONE,
    };
    deck_core_test_apply_event(&load1);

    assert(ui_library_stub_last_load_deck() == CTRL_DECK_1);

    ctrl_event_t load2 = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_SHIFT_LOAD_DECK2,
        .value = 1,
        .deck = CTRL_DECK_NONE,
    };
    deck_core_test_apply_event(&load2);

    assert(ui_library_stub_last_load_deck() == CTRL_DECK_2);
}
```

If the current test stubs use different function names, use the existing normal `CTRL_ID_LOAD_DECK*` test helper and duplicate its assertions with the shifted IDs.

- [ ] **Step 7: Implement P4 routing**

In `firmware/main-deck-p4/components/deck_core/deck_core.c`, wherever `CTRL_ID_LOAD_DECK1` and `CTRL_ID_LOAD_DECK2` are routed, add:

```c
    case CTRL_ID_SHIFT_LOAD_DECK1:
        if (ev->value) {
            ui_load_selected_track_to_deck(CTRL_DECK_1);
        }
        return true;
    case CTRL_ID_SHIFT_LOAD_DECK2:
        if (ev->value) {
            ui_load_selected_track_to_deck(CTRL_DECK_2);
        }
        return true;
```

Use the exact existing load function name from the normal load path. Do not create a new load path.

- [ ] **Step 8: Update docs**

Add a row to `docs/DDJ_FLX4_MIDI_MAP.md` near Load:

```markdown
| Shift + Load Deck 1 / Deck 2 | official MIDI list: `0x96/0x68`, `0x96/0x7A` | press/release | shifted global, deck from midino | `CTRL_ID_SHIFT_LOAD_DECK1`, `CTRL_ID_SHIFT_LOAD_DECK2` | UI Library | Implemented as same selected-track load path as normal Load | Host-tested from official PDF; hardware smoke pending |
```

Add IDs to `docs/CONTROL_LINK_PROTOCOL.md`.

- [ ] **Step 9: Verify**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
git diff --check
```

Expected: all pass.

- [ ] **Step 10: Commit**

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/control-board-s3/components/flx4_midi_host/flx4_map.c firmware/main-deck-p4/components/deck_core/deck_core.c tests/flx4_midi_host/test_flx4_map.c tests/control_link_protocol/s3_constants.c tests/control_link_protocol/p4_constants.c tests/control_link_protocol/test_control_link_protocol.c tests/deck_core_dual/test_deck_core_dual.c docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md
git commit -m "feat: map flx4 shifted load controls"
```

---

### Task 3: Add Headphone Level 14-Bit Input And Monitor Gain

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/include/flx4_map.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `tests/audio_engine/test_audio_engine.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`

**Behavior decision:** `HEADPHONE LEVEL` controls monitor/headphone output gain only. It does not affect master output. Raw `0..16383` maps linearly to scalar `0.0..1.0`, defaulting to unity until first MIDI value arrives.

- [ ] **Step 1: Add shared ID failing test**

Expose and assert `CTRL_ID_HEADPHONE_LEVEL` in the control-link parity files:

```c
int s3_ctrl_id_headphone_level(void) { return CTRL_ID_HEADPHONE_LEVEL; }
int p4_ctrl_id_headphone_level(void) { return CTRL_ID_HEADPHONE_LEVEL; }
```

In `test_control_link_protocol.c`:

```c
int s3_ctrl_id_headphone_level(void);
int p4_ctrl_id_headphone_level(void);

assert(s3_ctrl_id_headphone_level() == p4_ctrl_id_headphone_level());
assert(s3_ctrl_id_headphone_level() == CTRL_ID_HEADPHONE_LEVEL);
assert(s3_ctrl_id_headphone_level() != s3_ctrl_id_smart_cfx());
assert(s3_ctrl_id_headphone_level() == 0x7D);
```

- [ ] **Step 2: Add shared ID**

In both `control_link.h` files, in the system/global semantic ID block:

```c
/* Next free system/global semantic ID; avoid CTRL_NS_SYSTEM | offsets above 0x0F aliasing. */
#define CTRL_ID_HEADPHONE_LEVEL   0x7D
```

- [ ] **Step 3: Extend S3 map state**

In `firmware/control-board-s3/components/flx4_midi_host/include/flx4_map.h`, add:

```c
    flx4_14bit_state_t headphone_level;
```

next to `headphone_mix`.

- [ ] **Step 4: Add failing S3 14-bit test**

In `test_14bit_controls_emit_after_both_halves()`:

```c
    assert(!flx4_map_message(&state, MSG(0xB6, 0x0D, 0x14), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x2D, 0x24), &ev));
    expect_event(&ev, CTRL_TYPE_PITCH, CTRL_ID_HEADPHONE_LEVEL, (int16_t)((0x14 << 7) | 0x24));
```

In `test_snapshot_emits_known_absolute_controls_only()`, observe it and assert snapshot replay:

```c
    assert(!flx4_map_message(&state, MSG(0xB6, 0x0D, 0x52), &ev));
    assert(flx4_map_message(&state, MSG(0xB6, 0x2D, 0x53), &ev));
```

Update the expected count from `9` to `10`, and add:

```c
    expect_snapshot_event(&capture, CTRL_ID_HEADPHONE_LEVEL, (int16_t)((0x52 << 7) | 0x53));
```

- [ ] **Step 5: Implement S3 14-bit mapping**

In `flx4_map.c`, add constants:

```c
#define FLX4_CC_HEADPHONE_LEVEL_MSB 0x0D
#define FLX4_CC_HEADPHONE_LEVEL_LSB 0x2D
```

In `map_master_cc()`:

```c
    case FLX4_CC_HEADPHONE_LEVEL_MSB:
        return update_14bit(&state->headphone_level, true, data2, out, CTRL_ID_HEADPHONE_LEVEL);
    case FLX4_CC_HEADPHONE_LEVEL_LSB:
        return update_14bit(&state->headphone_level, false, data2, out, CTRL_ID_HEADPHONE_LEVEL);
```

In `flx4_map_emit_snapshot()`, after `headphone_mix`:

```c
        !emit_snapshot_14bit(&state->headphone_level,
                             CTRL_ID_HEADPHONE_LEVEL, cb, ctx, &count) ||
```

- [ ] **Step 6: Add audio_engine tests**

In `tests/audio_engine/test_audio_engine.c`, add:

```c
static void test_headphone_level_defaults_to_unity_and_scales_monitor(void)
{
    audio_engine_test_reset();

    EXPECT(audio_engine_set_headphone_level_raw(16383) == ESP_OK,
           "max headphone level accepted");
    EXPECT(audio_engine_get_headphone_level_raw() == 16383,
           "max headphone raw retained");

    EXPECT(audio_engine_set_headphone_level_raw(0) == ESP_OK,
           "min headphone level accepted");
    EXPECT(audio_engine_get_headphone_level_raw() == 0,
           "min headphone raw retained");

    EXPECT(audio_engine_set_headphone_level_raw(8192) == ESP_OK,
           "mid headphone level accepted");
    EXPECT(audio_engine_get_headphone_level_raw() == 8192,
           "mid headphone raw retained");
}
```

Call it from `main()`.

- [ ] **Step 7: Implement audio_engine API**

In `audio_engine.h`:

```c
esp_err_t audio_engine_set_headphone_level_raw(uint16_t raw);
uint16_t audio_engine_get_headphone_level_raw(void);
```

In `audio_engine.c`, add static state:

```c
static uint16_t s_headphone_level_raw = 16383u;
```

Add functions:

```c
esp_err_t audio_engine_set_headphone_level_raw(uint16_t raw)
{
    if (raw > 16383u) {
        return ESP_ERR_INVALID_ARG;
    }
    s_headphone_level_raw = raw;
    return ESP_OK;
}

uint16_t audio_engine_get_headphone_level_raw(void)
{
    return s_headphone_level_raw;
}
```

Apply the scalar in the existing headphone output path. Use this helper in the same file where monitor samples are mixed:

```c
static int16_t apply_headphone_level(int32_t sample)
{
    sample = (sample * (int32_t)s_headphone_level_raw) / 16383;
    if (sample > 32767) return 32767;
    if (sample < -32768) return -32768;
    return (int16_t)sample;
}
```

Wrap headphone left/right output samples through `apply_headphone_level()` only, not master output.

- [ ] **Step 8: Add deck_core routing test**

In `tests/deck_core_dual/test_deck_core_dual.c`, add:

```c
static void test_headphone_level_event_updates_audio_engine(void)
{
    deck_core_test_reset();

    ctrl_event_t ev = {
        .type = CTRL_EV_PITCH,
        .id = CTRL_ID_HEADPHONE_LEVEL,
        .value = 4096,
        .deck = CTRL_DECK_NONE,
    };
    deck_core_test_apply_event(&ev);

    assert(audio_engine_stub_headphone_level_raw == 4096);
}
```

If the audio engine stub does not expose `audio_engine_stub_headphone_level_raw`, add it to the local test stub following the existing mixer raw-value stub style.

- [ ] **Step 9: Implement deck_core routing**

In `deck_core.c`, route `CTRL_ID_HEADPHONE_LEVEL` with existing mixer pitch controls:

```c
    case CTRL_ID_HEADPHONE_LEVEL:
        (void)audio_engine_set_headphone_level_raw((uint16_t)ev->value);
        return true;
```

- [ ] **Step 10: Update docs**

Add row:

```markdown
| Headphone level | official MIDI list: `0xB6/0x0D+0x2D` | 14-bit MSB+LSB | global monitor | `CTRL_ID_HEADPHONE_LEVEL` | P4 monitor/headphone output gain | Implemented | Host-tested from official PDF; hardware smoke pending |
```

Add protocol row to `docs/CONTROL_LINK_PROTOCOL.md`.

- [ ] **Step 11: Verify**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
git diff --check
```

Expected: all pass.

- [ ] **Step 12: Commit**

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/control-board-s3/components/flx4_midi_host/include/flx4_map.h firmware/control-board-s3/components/flx4_midi_host/flx4_map.c firmware/main-deck-p4/components/audio_engine/include/audio_engine.h firmware/main-deck-p4/components/audio_engine/audio_engine.c firmware/main-deck-p4/components/deck_core/deck_core.c tests/flx4_midi_host/test_flx4_map.c tests/audio_engine/test_audio_engine.c tests/deck_core_dual/test_deck_core_dual.c tests/control_link_protocol/s3_constants.c tests/control_link_protocol/p4_constants.c tests/control_link_protocol/test_control_link_protocol.c docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md
git commit -m "feat: add flx4 headphone level control"
```

---

### Task 4: Add Shift + Smart CFX And Shift + Smart Fader

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`

**Behavior decision:** Map both shifted buttons to semantic IDs. P4 handles them as explicit toggles for alternate Smart modes only if the audio engine has an existing feature; otherwise P4 logs and stores last-pressed state without changing audio. Do not alias them to normal Smart CFX/Fader because the official PDF gives distinct addresses.

- [ ] **Step 1: Add shared IDs and parity tests**

Add to both `control_link.h` files:

```c
#define CTRL_ID_SMART_CFX_SHIFT        0x7E
#define CTRL_ID_SMART_FADER_SHIFT      0x7F
```

Expose constants in `s3_constants.c` and `p4_constants.c`:

```c
int s3_ctrl_id_smart_cfx_shift(void) { return CTRL_ID_SMART_CFX_SHIFT; }
int s3_ctrl_id_smart_fader_shift(void) { return CTRL_ID_SMART_FADER_SHIFT; }
int p4_ctrl_id_smart_cfx_shift(void) { return CTRL_ID_SMART_CFX_SHIFT; }
int p4_ctrl_id_smart_fader_shift(void) { return CTRL_ID_SMART_FADER_SHIFT; }
```

Assert equality in `test_control_link_protocol.c`.

- [ ] **Step 2: Add failing S3 tests**

In `test_smart_control_buttons()`:

```c
    assert(flx4_map_message(&state, MSG(0x96, 0x08, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_CFX_SHIFT, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x08, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_CFX_SHIFT, 0);

    assert(flx4_map_message(&state, MSG(0x96, 0x09, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_FADER_SHIFT, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x09, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_FADER_SHIFT, 0);
```

- [ ] **Step 3: Implement S3 mapping**

In `flx4_map.c`, add constants:

```c
#define FLX4_BTN_SMART_CFX_SHIFT    0x08
#define FLX4_BTN_SMART_FADER_SHIFT  0x09
```

In the global button branch:

```c
        if (msg->data1 == FLX4_BTN_SMART_CFX_SHIFT) {
            return emit_button(out, CTRL_ID_SMART_CFX_SHIFT, msg->data2 > 0 ? 1 : 0);
        }
        if (msg->data1 == FLX4_BTN_SMART_FADER_SHIFT) {
            return emit_button(out, CTRL_ID_SMART_FADER_SHIFT, msg->data2 > 0 ? 1 : 0);
        }
```

- [ ] **Step 4: Add P4 routing tests**

In `tests/deck_core_dual/test_deck_core_dual.c`, add:

```c
static void test_shifted_smart_buttons_are_accepted_without_toggling_primary_smart_state(void)
{
    deck_core_test_reset();

    ctrl_event_t cfx_shift = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_SMART_CFX_SHIFT,
        .value = 1,
        .deck = CTRL_DECK_NONE,
    };
    deck_core_test_apply_event(&cfx_shift);
    assert(!audio_engine_stub_smart_cfx_enabled);

    ctrl_event_t fader_shift = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_SMART_FADER_SHIFT,
        .value = 1,
        .deck = CTRL_DECK_NONE,
    };
    deck_core_test_apply_event(&fader_shift);
    assert(!audio_engine_stub_smart_fader_enabled);
}
```

Use existing stub state names if they differ from the names above.

- [ ] **Step 5: Implement P4 route as no-op accepted controls**

In `deck_core.c`, near normal Smart handling:

```c
    case CTRL_ID_SMART_CFX_SHIFT:
        if (ev->value) {
            ESP_LOGI(TAG, "Smart CFX shifted action accepted");
        }
        return true;
    case CTRL_ID_SMART_FADER_SHIFT:
        if (ev->value) {
            ESP_LOGI(TAG, "Smart Fader shifted action accepted");
        }
        return true;
```

- [ ] **Step 6: Update docs**

Add rows:

```markdown
| Smart CFX + Shift | official MIDI list: `0x96/0x08` | press/release | shifted global | `CTRL_ID_SMART_CFX_SHIFT` | P4 Smart CFX alternate action placeholder | Mapped and accepted; no audio behavior by product decision | Host-tested from official PDF; hardware smoke pending |
| Smart Fader + Shift | official MIDI list: `0x96/0x09` | press/release | shifted global | `CTRL_ID_SMART_FADER_SHIFT` | P4 Smart Fader alternate action placeholder | Mapped and accepted; no audio behavior by product decision | Host-tested from official PDF; hardware smoke pending |
```

- [ ] **Step 7: Verify and commit**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
git diff --check
```

Commit:

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/control-board-s3/components/flx4_midi_host/flx4_map.c firmware/main-deck-p4/components/deck_core/deck_core.c tests/flx4_midi_host/test_flx4_map.c tests/deck_core_dual/test_deck_core_dual.c tests/control_link_protocol/s3_constants.c tests/control_link_protocol/p4_constants.c tests/control_link_protocol/test_control_link_protocol.c docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md
git commit -m "feat: map flx4 shifted smart controls"
```

---

### Task 5: Add Shifted Beat FX Beat Buttons

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`

**Behavior decision:** Shifted Beat FX beat buttons perform coarse beat-size changes. Normal `0x4A/0x4B` remains one step. Shifted `0x66/0x6B` changes the Beat FX beat-size index by two steps, clamped to the existing range.

- [ ] **Step 1: Add shared IDs**

Add to both `control_link.h` files:

```c
#define CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT 0x83
#define CTRL_ID_BEAT_FX_BEAT_INC_SHIFT 0x84
```

Expose and assert parity in the same style as Task 4.

- [ ] **Step 2: Add failing S3 mapping tests**

In `test_beat_fx_controls()`:

```c
    assert(flx4_map_message(&state, MSG(0x94, 0x66, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT, 1);
    assert(flx4_map_message(&state, MSG(0x94, 0x6B, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BEAT_FX_BEAT_INC_SHIFT, 1);
```

- [ ] **Step 3: Implement S3 mapping**

In `flx4_map.c`, add:

```c
#define FLX4_BTN_BEAT_FX_BEAT_DEC_SHIFT 0x66
#define FLX4_BTN_BEAT_FX_BEAT_INC_SHIFT 0x6B
```

In `map_beat_fx_button()`:

```c
    case FLX4_BTN_BEAT_FX_BEAT_DEC_SHIFT:
        return emit_button(out, CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT, pressed);
    case FLX4_BTN_BEAT_FX_BEAT_INC_SHIFT:
        return emit_button(out, CTRL_ID_BEAT_FX_BEAT_INC_SHIFT, pressed);
```

- [ ] **Step 4: Add P4 behavior tests**

In `tests/deck_core_dual/test_deck_core_dual.c`, add:

```c
static void test_shifted_beat_fx_beat_buttons_step_by_two(void)
{
    deck_core_test_reset();

    ctrl_event_t inc_shift = beat_fx_button(CTRL_ID_BEAT_FX_BEAT_INC_SHIFT, 1);
    deck_core_test_apply_event(&inc_shift);
    assert(deck_core_test_get_beat_fx_state().beat_index == 2);

    ctrl_event_t dec_shift = beat_fx_button(CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT, 1);
    deck_core_test_apply_event(&dec_shift);
    assert(deck_core_test_get_beat_fx_state().beat_index == 0);
}
```

Use the existing beat FX state helper name from the current Beat FX tests; if it exposes beat fraction instead of index, assert the equivalent two-step value.

- [ ] **Step 5: Implement P4 behavior**

In `deck_core.c`, near normal `CTRL_ID_BEAT_FX_BEAT_DEC`/`INC` handling, add:

```c
    case CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT:
        if (ev->value) {
            beat_fx_adjust_beat_index(-2);
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_INC_SHIFT:
        if (ev->value) {
            beat_fx_adjust_beat_index(2);
        }
        return true;
```

If the existing helper takes `bool increase`, add a small local helper:

```c
static void beat_fx_adjust_beat_index_by(int delta)
{
    while (delta > 0) {
        beat_fx_adjust_beat_index(1);
        delta--;
    }
    while (delta < 0) {
        beat_fx_adjust_beat_index(-1);
        delta++;
    }
}
```

Use existing clamping behavior.

- [ ] **Step 6: Update docs**

Add row:

```markdown
| Beat FX beat left/right + Shift | official MIDI list: `0x94/0x66`, `0x94/0x6B` | press/release | shifted FX section | `CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT`, `CTRL_ID_BEAT_FX_BEAT_INC_SHIFT` | P4 Beat FX state model | Implemented as two-step beat-size change | Host-tested from official PDF; hardware smoke pending |
```

- [ ] **Step 7: Verify and commit**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
git diff --check
```

Commit:

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/control-board-s3/components/flx4_midi_host/flx4_map.c firmware/main-deck-p4/components/deck_core/deck_core.c tests/flx4_midi_host/test_flx4_map.c tests/deck_core_dual/test_deck_core_dual.c tests/control_link_protocol/s3_constants.c tests/control_link_protocol/p4_constants.c tests/control_link_protocol/test_control_link_protocol.c docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md
git commit -m "feat: add shifted beat fx beat controls"
```

---

### Task 6: Explicitly Ignore Fader Start MIDI Messages

**Files:**
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`

**Behavior decision:** Do not implement channel-fader start or crossfader start playback automation for standalone firmware now. The official PDF messages `0x90/0x66`, `0x90/0x52`, `0x91/0x66`, `0x91/0x52` are generated by fader movement edges and could unexpectedly start/stop decks. The S3 should explicitly ignore these messages and tests should lock that decision.

- [ ] **Step 1: Add failing ignore tests**

In `test_unsupported_messages_are_ignored()`:

```c
    assert(!flx4_map_message(&state, MSG(0x90, 0x66, 0x7F), &ev));
    assert(!flx4_map_message(&state, MSG(0x90, 0x52, 0x7F), &ev));
    assert(!flx4_map_message(&state, MSG(0x91, 0x66, 0x7F), &ev));
    assert(!flx4_map_message(&state, MSG(0x91, 0x52, 0x7F), &ev));
```

- [ ] **Step 2: Run test**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
```

Expected: this may already pass because unknown messages are ignored. If it passes, keep the tests as a regression guard.

- [ ] **Step 3: Add named constants and explicit ignore**

In `flx4_map.c`, add:

```c
#define FLX4_BTN_FADER_START_PLAY 0x66
#define FLX4_BTN_FADER_START_CUE  0x52
```

In `map_deck_button()` before `default`:

```c
    case FLX4_BTN_FADER_START_PLAY:
    case FLX4_BTN_FADER_START_CUE:
        return false;
```

- [ ] **Step 4: Update docs**

Add row:

```markdown
| Channel/crossfader start generated messages | official MIDI list: `0x90/0x66`, `0x90/0x52`, `0x91/0x66`, `0x91/0x52` | generated on fader edge movement | deck-local pseudo-button | none | none | Explicitly ignored to avoid surprise playback automation | Host-tested from official PDF; hardware smoke not required unless product decision changes |
```

- [ ] **Step 5: Verify and commit**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
git diff --check
```

Commit:

```powershell
git add firmware/control-board-s3/components/flx4_midi_host/flx4_map.c tests/flx4_midi_host/test_flx4_map.c docs/DDJ_FLX4_MIDI_MAP.md
git commit -m "docs: lock fader start messages out of scope"
```

---

### Task 7: Add LED/Output Candidates That Have P4-Owned State

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/control_link/flx4_led_midi.c`
- Modify: `firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/flx4_midi_host/test_flx4_led_midi.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`

**Behavior decision:** Implement LED outputs only where firmware already owns enough state. Do not implement Key Shift, Stem, or Keyboard LEDs beyond existing mode LEDs. Add track-load illumination because P4 owns deck-loaded state. Add Censor LED because P4 owns `censor_active`. Add Loop Adjust LEDs as momentary flashes on adjust press, not persistent edit mode. Add Beat Jump and Sampler pad LED packet support, but P4 only sends Beat Jump pad LED state if existing Beat Jump state selects a pad; Sampler remains packet-support only until sampler behavior exists.

- [ ] **Step 1: Add LED ID parity tests**

Expose new LEDs in `s3_constants.c` and `p4_constants.c`:

```c
int s3_led_censor(void) { return LED_CENSOR; }
int s3_led_cue_shift(void) { return LED_CUE_SHIFT; }
int s3_led_loop_adjust_in(void) { return LED_LOOP_ADJUST_IN; }
int s3_led_loop_adjust_out(void) { return LED_LOOP_ADJUST_OUT; }
int s3_led_track_load_deck1(void) { return LED_TRACK_LOAD_DECK1; }
int s3_led_track_load_deck2(void) { return LED_TRACK_LOAD_DECK2; }
int p4_led_censor(void) { return LED_CENSOR; }
int p4_led_cue_shift(void) { return LED_CUE_SHIFT; }
int p4_led_loop_adjust_in(void) { return LED_LOOP_ADJUST_IN; }
int p4_led_loop_adjust_out(void) { return LED_LOOP_ADJUST_OUT; }
int p4_led_track_load_deck1(void) { return LED_TRACK_LOAD_DECK1; }
int p4_led_track_load_deck2(void) { return LED_TRACK_LOAD_DECK2; }
```

Add prototypes and equality asserts in `test_control_link_protocol.c`.

- [ ] **Step 2: Add shared LED IDs**

In both `control_link.h` files, replace:

```c
#define LED_REMOTE_COUNT 53
```

or:

```c
    LED_REMOTE_COUNT,
```

with the new IDs listed in the "Shared ID Allocation" section. For the P4 enum style, append the enum values before `LED_REMOTE_COUNT`:

```c
    LED_CENSOR,
    LED_CUE_SHIFT,
    LED_LOOP_ADJUST_IN,
    LED_LOOP_ADJUST_OUT,
    LED_TRACK_LOAD_DECK1,
    LED_TRACK_LOAD_DECK2,
    LED_BEAT_JUMP_PAD_1,
    LED_BEAT_JUMP_PAD_2,
    LED_BEAT_JUMP_PAD_3,
    LED_BEAT_JUMP_PAD_4,
    LED_BEAT_JUMP_PAD_5,
    LED_BEAT_JUMP_PAD_6,
    LED_BEAT_JUMP_PAD_7,
    LED_BEAT_JUMP_PAD_8,
    LED_SAMPLER_PAD_1,
    LED_SAMPLER_PAD_2,
    LED_SAMPLER_PAD_3,
    LED_SAMPLER_PAD_4,
    LED_SAMPLER_PAD_5,
    LED_SAMPLER_PAD_6,
    LED_SAMPLER_PAD_7,
    LED_SAMPLER_PAD_8,
    LED_REMOTE_COUNT,
```

- [ ] **Step 3: Add LED packet tests**

In `tests/flx4_midi_host/test_flx4_led_midi.c`, add:

```c
static void test_official_candidate_led_packets(void)
{
    expect_packet(LED_CENSOR, 1, CTRL_DECK_1, 0x09, 0x90, 0x0E, 0x7F);
    expect_packet(LED_CENSOR, 0, CTRL_DECK_2, 0x09, 0x91, 0x0E, 0x00);
    expect_packet(LED_CUE_SHIFT, 1, CTRL_DECK_1, 0x09, 0x90, 0x48, 0x7F);
    expect_packet(LED_LOOP_ADJUST_IN, 1, CTRL_DECK_1, 0x09, 0x90, 0x4C, 0x7F);
    expect_packet(LED_LOOP_ADJUST_OUT, 1, CTRL_DECK_2, 0x09, 0x91, 0x4E, 0x7F);

    expect_packet(LED_TRACK_LOAD_DECK1, 1, CTRL_DECK_1, 0x09, 0x9F, 0x00, 0x7F);
    expect_packet(LED_TRACK_LOAD_DECK2, 1, CTRL_DECK_2, 0x09, 0x9F, 0x01, 0x7F);

    expect_packet(LED_BEAT_JUMP_PAD_1, 1, CTRL_DECK_1, 0x09, 0x97, 0x20, 0x7F);
    expect_packet(LED_BEAT_JUMP_PAD_8, 0, CTRL_DECK_2, 0x09, 0x99, 0x27, 0x00);
    expect_packet(LED_SAMPLER_PAD_1, 1, CTRL_DECK_1, 0x09, 0x97, 0x30, 0x7F);
    expect_packet(LED_SAMPLER_PAD_8, 0, CTRL_DECK_2, 0x09, 0x99, 0x37, 0x00);
}
```

Call it from `main()`.

- [ ] **Step 4: Implement S3 LED MIDI packet mapping**

In `flx4_led_midi.c`, extend `note_for_led()`:

```c
    if (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_8) {
        *note = (uint8_t)(0x20u + (led - LED_BEAT_JUMP_PAD_1));
        return true;
    }
    if (led >= LED_SAMPLER_PAD_1 && led <= LED_SAMPLER_PAD_8) {
        *note = (uint8_t)(0x30u + (led - LED_SAMPLER_PAD_1));
        return true;
    }
```

Add switch cases:

```c
    case LED_CENSOR:
        *note = 0x0E;
        return true;
    case LED_CUE_SHIFT:
        *note = 0x48;
        return true;
    case LED_LOOP_ADJUST_IN:
        *note = 0x4C;
        return true;
    case LED_LOOP_ADJUST_OUT:
        *note = 0x4E;
        return true;
    case LED_TRACK_LOAD_DECK1:
        *note = 0x00;
        return true;
    case LED_TRACK_LOAD_DECK2:
        *note = 0x01;
        return true;
```

In `flx4_led_midi_build_packet()`, before normal note status selection:

```c
    if (led == LED_TRACK_LOAD_DECK1 || led == LED_TRACK_LOAD_DECK2) {
        packet[0] = 0x09;
        packet[1] = 0x9F;
        packet[2] = (led == LED_TRACK_LOAD_DECK1) ? 0x00 : 0x01;
        packet[3] = (state != 0) ? 0x7F : 0x00;
        return true;
    }
```

Add Beat Jump/Sampler pad LEDs to the pad-status branch:

```c
        (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_8) ||
        (led >= LED_SAMPLER_PAD_1 && led <= LED_SAMPLER_PAD_8) ||
```

- [ ] **Step 5: Add P4 LED behavior tests**

In `tests/deck_core_dual/test_deck_core_dual.c`, add:

```c
static void test_censor_and_loop_adjust_leds_follow_ext_actions(void)
{
    deck_core_test_reset();

    ctrl_event_t censor_press = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_CENSOR, true);
    deck_core_test_apply_event(&censor_press);
    assert(control_link_stub_last_led_state(LED_CENSOR, CTRL_DECK_1) == 1);

    ctrl_event_t censor_release = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_CENSOR, false);
    deck_core_test_apply_event(&censor_release);
    assert(control_link_stub_last_led_state(LED_CENSOR, CTRL_DECK_1) == 0);

    ctrl_event_t loop_in = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, true);
    deck_core_test_apply_event(&loop_in);
    assert(control_link_stub_last_led_state(LED_LOOP_ADJUST_IN, CTRL_DECK_2) == 1);

    ctrl_event_t loop_in_release = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, false);
    deck_core_test_apply_event(&loop_in_release);
    assert(control_link_stub_last_led_state(LED_LOOP_ADJUST_IN, CTRL_DECK_2) == 0);
}
```

Add a track-load illumination test beside load tests:

```c
static void test_track_load_illumination_follows_loaded_decks(void)
{
    deck_core_test_reset();

    ctrl_event_t load1 = {
        .type = CTRL_EV_BUTTON,
        .id = CTRL_ID_LOAD_DECK1,
        .value = 1,
        .deck = CTRL_DECK_NONE,
    };
    deck_core_test_apply_event(&load1);

    assert(control_link_stub_last_led_state(LED_TRACK_LOAD_DECK1, CTRL_DECK_1) == 1);
}
```

If loaded state is asynchronous in tests, call the existing load-complete helper used by current UI/library load tests before asserting.

- [ ] **Step 6: Implement P4 LED state**

In `handle_censor()` in `deck_core.c`, send:

```c
control_link_send_led_deck(LED_CENSOR, pressed ? 1u : 0u, deck);
```

In loop-adjust handling, send `LED_LOOP_ADJUST_IN` or `LED_LOOP_ADJUST_OUT` on press and clear on release:

```c
control_link_send_led_deck(adjust_in ? LED_LOOP_ADJUST_IN : LED_LOOP_ADJUST_OUT,
                           pressed ? 1u : 0u,
                           deck);
```

In successful deck load completion, send:

```c
control_link_send_led_deck(deck == CTRL_DECK_1 ? LED_TRACK_LOAD_DECK1 : LED_TRACK_LOAD_DECK2,
                           1u,
                           deck);
```

Do not send Keyboard, Stem, or Key Shift pad LEDs in this task.

- [ ] **Step 7: Extend reconnect snapshot**

In `flx4_led_snapshot.c`, increase `FLX4_LED_SNAPSHOT_COUNT` and append:

```c
    LED_CENSOR,
    LED_LOOP_ADJUST_IN,
    LED_LOOP_ADJUST_OUT,
    LED_TRACK_LOAD_DECK1,
    LED_TRACK_LOAD_DECK2,
    LED_BEAT_JUMP_PAD_1,
    LED_BEAT_JUMP_PAD_2,
    LED_BEAT_JUMP_PAD_3,
    LED_BEAT_JUMP_PAD_4,
    LED_BEAT_JUMP_PAD_5,
    LED_BEAT_JUMP_PAD_6,
    LED_BEAT_JUMP_PAD_7,
    LED_BEAT_JUMP_PAD_8,
    LED_SAMPLER_PAD_1,
    LED_SAMPLER_PAD_2,
    LED_SAMPLER_PAD_3,
    LED_SAMPLER_PAD_4,
    LED_SAMPLER_PAD_5,
    LED_SAMPLER_PAD_6,
    LED_SAMPLER_PAD_7,
    LED_SAMPLER_PAD_8,
```

In `snapshot_value()`, return:

```c
    case LED_CENSOR:
        return input->deck_state[deck].censor_active ? 1u : 0u;
    case LED_TRACK_LOAD_DECK1:
        return input->deck_loaded[CTRL_DECK_1] ? 1u : 0u;
    case LED_TRACK_LOAD_DECK2:
        return input->deck_loaded[CTRL_DECK_2] ? 1u : 0u;
```

If `flx4_led_snapshot_input_t` does not yet have `deck_loaded`, add:

```c
bool deck_loaded[2];
```

and populate it from `deck_core.c`.

Return `0` for Sampler pad LEDs until sampler slot state exists. Return Beat Jump pad selection only if current `deck_core` exposes selected beat jump pad; otherwise return `0` and keep packet support only.

- [ ] **Step 8: Update docs**

Update candidate LED rows in `docs/DDJ_FLX4_MIDI_MAP.md`:

```markdown
| Play + Shift / Censor LEDs | official list: `0x90/0x0E`, `0x91/0x0E` | P4 `deck_state_t.censor_active` | P4 `deck_core` | Implemented | Host-tested from official PDF; hardware smoke pending |
| Cue + Shift / track-start LEDs | official list: `0x90/0x48`, `0x91/0x48` | future shifted cue/track-start indicator | P4 `deck_core` | Packet support only | No behavior LED state yet |
| Shift + Loop In adjust LEDs | official list: `0x90/0x4C`, `0x91/0x4C` | momentary loop-adjust press | P4 `deck_core` | Implemented momentary output | Host-tested from official PDF; hardware smoke pending |
| Shift + Loop Out adjust LEDs | official list output: `0x90/0x4E`, `0x91/0x4E` | momentary loop-adjust press | P4 `deck_core` | Implemented momentary output | Host-tested from official PDF; hardware smoke pending |
| Loaded / Track Load Illumination | official list: D1 `0x9F/0x00`, D2 `0x9F/0x01` | deck has loaded track | P4 deck/library state | Implemented | Host-tested from official PDF; hardware smoke pending |
| Beat Jump pad LEDs | normal D1 `0x97/0x20..0x27`, normal D2 `0x99/0x20..0x27` | future beat-jump pad state | P4 beat jump/pad mode state | Packet support; behavior state only if selected pad exists | Host-tested packet build; hardware smoke pending |
| Sampler pad LEDs | left normal `0x97/0x30..0x37`, right normal `0x99/0x30..0x37` | sampler slot loaded | sampler model | Packet support only; sampler behavior excluded for now | Host-tested packet build; hardware smoke pending |
```

Do not add Key Shift, Stem, or Keyboard LED behavior rows beyond explicit exclusion notes.

- [ ] **Step 9: Verify and commit**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
git diff --check
```

Commit:

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/control-board-s3/components/control_link/flx4_led_midi.c firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c firmware/main-deck-p4/components/deck_core/deck_core.c tests/flx4_midi_host/test_flx4_led_midi.c tests/deck_core_dual/test_deck_core_dual.c tests/control_link_protocol/s3_constants.c tests/control_link_protocol/p4_constants.c tests/control_link_protocol/test_control_link_protocol.c docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md
git commit -m "feat: add flx4 official led output candidates"
```

---

### Task 8: Documentation And Final Verification

**Files:**
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Update development plan**

Add a completed phase note:

```markdown
### FLX4 Official MIDI Gap Closure

- Corrected Master Cue + Shift to official `0x96/0x78`.
- Added shifted Load, Headphone Level, shifted Smart controls, shifted Beat FX beat controls, and selected official LED outputs.
- Fader-start generated messages are explicitly ignored to avoid surprise playback automation.
- Key Shift, Stem, and Keyboard behavior remain out of scope by product decision.
```

- [ ] **Step 2: Update startup checklist**

Add hardware smoke checklist:

```markdown
- [ ] Verify Master Cue + Shift emits/toggles through `0x96/0x78`.
- [ ] Verify Shift + Load Deck 1/2 addresses `0x96/0x68` and `0x96/0x7A`.
- [ ] Verify Headphone Level `0xB6/0x0D+0x2D` changes monitor output without changing main output.
- [ ] Verify Shift + Smart CFX/Fader `0x96/0x08`, `0x96/0x09` are received and do not alter primary Smart states.
- [ ] Verify shifted Beat FX beat buttons `0x94/0x66`, `0x94/0x6B`.
- [ ] Verify fader-start messages are ignored.
- [ ] Verify Censor, Loop Adjust, Track Load, Beat Jump, and Sampler LED packet outputs where implemented.
```

- [ ] **Step 3: Run host verification**

Run:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
git diff --check
git status --short
```

Expected: both test suites pass, diff check has no output, status only shows intended docs if this task has not been committed yet.

- [ ] **Step 4: Run firmware builds**

Run S3:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd C:\Users\Daniel\.config\superpowers\worktrees\DDJ-FFL4\codex-flx4-shifted-extended-controls\firmware\control-board-s3
idf.py build
```

Expected: `Project build complete.`

Run P4:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd C:\Users\Daniel\.config\superpowers\worktrees\DDJ-FFL4\codex-flx4-shifted-extended-controls\firmware\main-deck-p4
idf.py build
```

Expected: `Project build complete.`

- [ ] **Step 5: Commit docs**

```powershell
git add docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md
git commit -m "docs: close flx4 official midi gaps"
```

- [ ] **Step 6: Push**

```powershell
git status --short
git push
```

Expected: clean status and branch pushed to `origin/codex/flx4-shifted-extended-controls`.

---

## Self-Review

- Spec coverage:
  - Point 1 Master Cue + Shift bug: Task 1.
  - Point 2 Shift + Load: Task 2.
  - Point 3 Headphone Level: Task 3.
  - Point 4 Shift + Smart CFX/Fader: Task 4.
  - Point 5 Shifted Beat FX beat buttons: Task 5.
  - Point 6 Fader start generated messages: Task 6.
  - Point 7 LED/output candidates: Task 7.
  - Final docs/build/push: Task 8.
- Exclusions:
  - Key Shift behavior: excluded in Scope Rules and Task 7.
  - Stem behavior: excluded in Scope Rules and Task 7.
  - Keyboard behavior: excluded in Scope Rules and Task 7.
- Placeholder scan:
  - No `TBD` or open-ended implementation placeholders remain.
  - Any references to existing helper names that may differ are paired with explicit instruction to reuse the already-existing helper in the same test area.
- Type consistency:
  - New IDs use the existing `CTRL_TYPE_BUTTON` or `CTRL_TYPE_PITCH` event shapes.
  - New LED IDs follow existing `led_id_t` / `LED_REMOTE_COUNT` patterns.
  - S3 and P4 `control_link.h` must be edited together and parity-tested.
