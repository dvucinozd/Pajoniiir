# FLX4 Extended Controls Salvage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Safely salvage verified work from `codex/flx4-extended-controls` into current `master` without regressing Browse behavior, FLX4 LED fallback stability, or P4 audio output pacing.

**Architecture:** Current `master` remains authoritative. The old branch is treated as a reference/source branch, not a merge target. Each slice is reintroduced behind host tests and firmware builds, with P4 remaining authoritative for deck state, LEDs, audio state, and UI state.

**Tech Stack:** ESP-IDF v5.5, ESP32-S3 USB MIDI host firmware, ESP32-P4 playback/UI/audio firmware, existing `0xA5` UART `control_link`, C host tests under `tests/`, PowerShell build/test scripts.

---

## Baseline Facts

- Current safe base: `master` at `148a868 fix(audio): include render time in output pacing`.
- Source branch: `origin/codex/flx4-extended-controls` at `d17eb99 docs: record Smart controls MIDI input capture`.
- `codex/flx4-extended-controls` is not merged into `master`.
- Direct merge is rejected because it conflicts with current:
  - Browse press Library/Overview toggle behavior.
  - S3 FLX4 LED fallback crash guard.
  - P4 `audio_output_timing` pacing fix.
  - Current P4 audio scheduling baseline.
- Current uncommitted project note before executing this plan:
  - `docs/DEVELOPMENT_PLAN.md` contains the audio limiter and preload/index timing follow-up.

## File Structure

### Branch and audit files

- Modify: `docs/DEVELOPMENT_PLAN.md`
- Create or modify: `docs/superpowers/plans/2026-06-20-flx4-extended-controls-salvage.md`

### Documentation/capture salvage

- Create from source branch and reconcile: `docs/validation/FLX4_SMART_INPUT_CAPTURE.md`
- Create from source branch and reconcile: `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`
- Modify if needed: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify if needed: `docs/STARTUP_CHECKLIST.md`
- Modify if needed: `docs/DEVELOPMENT_PLAN.md`

### LED reconnect/snapshot salvage

- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c`
- Modify: `firmware/control-board-s3/main/app_main.c`
- Modify: `firmware/main-deck-p4/components/control_link/control_link_uart.c`
- Create or reintroduce after review: `firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c`
- Create or reintroduce after review: `firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h`
- Modify: `firmware/main-deck-p4/components/control_link/CMakeLists.txt`
- Test: `tests/control_link_protocol/test_control_link_protocol.c`
- Test: `tests/control_link_protocol/s3_constants.c`
- Test: `tests/control_link_protocol/p4_constants.c`
- Test: `tests/flx4_led_snapshot/test_flx4_led_snapshot.c`
- Test: `tests/panel_leds/test_panel_leds.c`

### Smart controls input salvage

- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/include/flx4_map.h`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`

### Deferred source branch areas

Keep these out of this salvage pass unless explicitly approved after P4 audio stabilization:

- `firmware/main-deck-p4/components/audio_engine/audio_channel_strip.c`
- `firmware/main-deck-p4/components/audio_engine/audio_fx_*.c`
- `firmware/main-deck-p4/components/audio_engine/audio_smart_*.c`
- `firmware/main-deck-p4/components/deck_core/beat_sync.c`
- `firmware/main-deck-p4/components/deck_core/deck_transport_modes.c`
- `firmware/main-deck-p4/components/deck_core/smart_fader.c`
- `firmware/main-deck-p4/components/ui/ui_mixer_view.c`
- `firmware/main-deck-p4/components/ui/ui_performance_tabs.c`
- all related tests for audio FX, beat sync, sampler, smart fader DSP, smart CFX DSP, and mixer UI.

---

### Task 1: Create clean salvage branch

**Files:**
- Modify: none expected.

- [ ] **Step 1: Confirm the working tree**

Run:

```powershell
git status -sb
git diff --check
```

Expected:

```text
## master...origin/master
 M docs/DEVELOPMENT_PLAN.md
?? docs/superpowers/plans/2026-06-20-flx4-extended-controls-salvage.md
```

`git diff --check` must exit `0`. Windows LF/CRLF warnings are acceptable if there are no whitespace errors.

- [ ] **Step 2: Commit the planning/follow-up documentation**

Run:

```powershell
git add docs/DEVELOPMENT_PLAN.md docs/superpowers/plans/2026-06-20-flx4-extended-controls-salvage.md
git commit -m "docs: plan FLX4 extended controls salvage"
```

Expected: commit succeeds and `git status -sb` shows clean `master`.

- [ ] **Step 3: Create integration branch from current master**

Run:

```powershell
git switch -c codex/flx4-extended-controls-salvage
```

Expected:

```text
Switched to a new branch 'codex/flx4-extended-controls-salvage'
```

- [ ] **Step 4: Confirm the source branch remains reference-only**

Run:

```powershell
git merge-base --is-ancestor origin/codex/flx4-extended-controls HEAD
if ($LASTEXITCODE -eq 0) { throw "Unexpected: source branch is already merged" }
git diff --name-status HEAD...origin/codex/flx4-extended-controls | Measure-Object
```

Expected: first command exits non-zero; second command reports a large count. Do not run `git merge origin/codex/flx4-extended-controls`.

---

### Task 2: Salvage verified capture documentation only

**Files:**
- Create: `docs/validation/FLX4_SMART_INPUT_CAPTURE.md`
- Create: `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Import validation documents from source branch**

Run:

```powershell
git show origin/codex/flx4-extended-controls:docs/validation/FLX4_SMART_INPUT_CAPTURE.md > $env:TEMP\FLX4_SMART_INPUT_CAPTURE.md
git show origin/codex/flx4-extended-controls:docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md > $env:TEMP\FLX4_LED_MIDI_OUT_CAPTURE.md
```

Then copy the contents into the target files using the repository editing tool so both files are tracked under `docs/validation/`.

Expected target facts in `docs/validation/FLX4_SMART_INPUT_CAPTURE.md`:

```text
SMART CFX: status 0x96, note 0x00, press 0x7F, release 0x00
SMART FADER: status 0x96, note 0x01, press 0x7F, release 0x00
S3 translates press/release only; P4 owns enabled state and LED output
```

Expected target facts in `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`:

```text
MVP reconnect verified for transport Play/Cue/PFL LEDs
Smart CFX LED: status 0x96, note 0x00, off 0x00, on 0x7F
Smart Fader LED: status 0x96, note 0x01, off 0x00, on 0x7F
```

- [ ] **Step 2: Reconcile Browse documentation with current master**

Search:

```powershell
rg -n "Browse|BROWSE|Library|Overview" docs/DDJ_FLX4_MIDI_MAP.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md
```

Required final wording:

```text
Browse press toggles Library/Overview on P4. Load 1 and Load 2 load the selected track to Deck 1/Deck 2. Browse rotate moves the selection by one row per physical detent.
```

Do not reintroduce old wording that says Browse press only opens/focuses Library.

- [ ] **Step 3: Verify documentation-only diff**

Run:

```powershell
git diff --check
git diff --name-only
```

Expected changed files only:

```text
docs/validation/FLX4_SMART_INPUT_CAPTURE.md
docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md
docs/DDJ_FLX4_MIDI_MAP.md
docs/DEVELOPMENT_PLAN.md
docs/STARTUP_CHECKLIST.md
```

If `docs/DDJ_FLX4_MIDI_MAP.md`, `docs/DEVELOPMENT_PLAN.md`, or `docs/STARTUP_CHECKLIST.md` do not need edits after review, they may be absent from the diff.

- [ ] **Step 4: Commit documentation salvage**

Run:

```powershell
git add docs/validation/FLX4_SMART_INPUT_CAPTURE.md docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md docs/DDJ_FLX4_MIDI_MAP.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md
git commit -m "docs: salvage verified FLX4 capture notes"
```

Expected: commit succeeds.

---

### Task 3: Salvage LED reconnect protocol without extended control baggage

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`

- [ ] **Step 1: Add only the FLX4 connection state ID**

Add the same constants to both S3 and P4 `control_link.h` files:

```c
#define CTRL_ID_FLX4_CONNECTION (CTRL_NS_SYSTEM | 0x00)

typedef enum {
    CTRL_FLX4_DISCONNECTED = 0,
    CTRL_FLX4_CONNECTED = 1,
} ctrl_flx4_connection_t;
```

Keep these current `master` definitions unchanged:

```c
#define CTRL_TYPE_STATE    0x82
#define CTRL_NS_DECK1   0x10
#define CTRL_NS_DECK2   0x20
#define CTRL_NS_MIXER   0x30
#define CTRL_NS_BROWSER 0x40
#define CTRL_NS_SYSTEM  0x70
```

Do not add `CTRL_TYPE_DECK_ACTION`, `CTRL_TYPE_PERFORMANCE`, `CTRL_TYPE_FX_ACTION`, `CTRL_TYPE_FX_VALUE`, `CTRL_NS_MONITOR`, EQ IDs, FX IDs, pad mode IDs, or transport extension IDs in this task.

- [ ] **Step 2: Add protocol parity tests**

In `tests/control_link_protocol/test_control_link_protocol.c`, add assertions equivalent to:

```c
assert(S3_CTRL_TYPE_STATE == P4_CTRL_TYPE_STATE);
assert(S3_CTRL_ID_FLX4_CONNECTION == P4_CTRL_ID_FLX4_CONNECTION);
assert(S3_CTRL_FLX4_DISCONNECTED == P4_CTRL_FLX4_DISCONNECTED);
assert(S3_CTRL_FLX4_CONNECTED == P4_CTRL_FLX4_CONNECTED);
```

Expose the S3/P4 constants in `tests/control_link_protocol/s3_constants.c` and `tests/control_link_protocol/p4_constants.c` using the existing constant-export pattern in those files.

- [ ] **Step 3: Run protocol host test**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_s3_host_tests.ps1
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: both scripts exit `0`. If a script reports a missing optional test directory that is not present on `master`, record it before changing the script.

- [ ] **Step 4: Commit protocol slice**

Run:

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h tests/control_link_protocol
git commit -m "feat(control-link): add FLX4 connection state"
```

Expected: commit succeeds.

---

### Task 4: Salvage S3 FLX4 connection publication and keep LED fallback stable

**Files:**
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/include/flx4_midi_host.h`
- Modify: `firmware/control-board-s3/main/app_main.c`
- Modify: `tests/flx4_midi_host/test_flx4_midi_host.c`
- Modify: `tests/panel_leds/test_panel_leds.c`

- [ ] **Step 1: Find current connection lifecycle hooks**

Run:

```powershell
rg -n "connect|disconnect|mounted|unmounted|tuh_|CTRL_ID_FLX4_CONNECTION|panel_led" firmware/control-board-s3/components/flx4_midi_host firmware/control-board-s3/main tests
```

Expected: identify the DDJ-FLX4 USB attach/detach path and the existing panel LED fallback test.

- [ ] **Step 2: Add S3 publication on USB attach/detach**

Implement the minimal behavior:

```c
control_link_send_semantic(CTRL_TYPE_STATE, CTRL_ID_FLX4_CONNECTION, CTRL_FLX4_CONNECTED);
```

when the FLX4 interface is ready, and:

```c
control_link_send_semantic(CTRL_TYPE_STATE, CTRL_ID_FLX4_CONNECTION, CTRL_FLX4_DISCONNECTED);
```

when the FLX4 interface is detached or no longer usable.

Do not store playback state on S3. Do not infer LED state on S3. S3 only publishes connection state.

- [ ] **Step 3: Preserve the panel LED no-op guard**

Run:

```powershell
rg -n "panel_led_set|pre-initial|no-op|mutex|initialized" firmware/control-board-s3/components/panel_io tests/panel_leds
```

Expected: confirm `panel_led_set()` remains safe before legacy `panel_io` initialization. If adding connection publication touches this path, keep the test that proves FLX4 translator mode does not crash on P4 LED frames.

- [ ] **Step 4: Add host tests for connection publications**

In `tests/flx4_midi_host/test_flx4_midi_host.c`, add cases that assert:

```text
FLX4 attach publishes CTRL_TYPE_STATE / CTRL_ID_FLX4_CONNECTION / CTRL_FLX4_CONNECTED exactly once per attach transition.
FLX4 detach publishes CTRL_TYPE_STATE / CTRL_ID_FLX4_CONNECTION / CTRL_FLX4_DISCONNECTED exactly once per detach transition.
Repeated attach without detach does not spam duplicate connected frames.
```

Use the existing test double/stub style already present in the file. Do not introduce a real USB dependency into the host test.

- [ ] **Step 5: Run S3 host tests and S3 firmware build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_s3_host_tests.ps1
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
cd D:\Documents\DDJ-FFL4
```

Expected: host tests and S3 build exit `0`.

- [ ] **Step 6: Commit S3 connection publication**

Run:

```powershell
git add firmware/control-board-s3/components/flx4_midi_host firmware/control-board-s3/main/app_main.c tests/flx4_midi_host tests/panel_leds
git commit -m "feat(s3): publish FLX4 USB connection state"
```

Expected: commit succeeds.

---

### Task 5: Salvage P4 LED snapshot request and publication

**Files:**
- Modify: `firmware/main-deck-p4/components/control_link/control_link_uart.c`
- Create or modify: `firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c`
- Create or modify: `firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h`
- Modify: `firmware/main-deck-p4/components/control_link/CMakeLists.txt`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify if required by current code: `firmware/main-deck-p4/components/ui/ui.c`
- Test: `tests/flx4_led_snapshot/test_flx4_led_snapshot.c`
- Test: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Inspect source branch implementation before copying**

Run:

```powershell
git show origin/codex/flx4-extended-controls:firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c
git show origin/codex/flx4-extended-controls:firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h
git show origin/codex/flx4-extended-controls:tests/flx4_led_snapshot/test_flx4_led_snapshot.c
```

Expected: identify only snapshot/diff/retry logic. Do not copy unrelated FX, beat sync, sampler, smart layer, or UI performance-tab dependencies.

- [ ] **Step 2: Add snapshot module**

The module must expose these behaviors:

```text
flx4_led_snapshot_init clears last-sent cache.
flx4_led_snapshot_publish(force=false) sends only changed MVP LED states after successful send.
flx4_led_snapshot_publish(force=true) sends every MVP LED state, including off values.
If a send fails, that LED remains invalid so the next publish retries it.
```

MVP LED state scope for this task:

```text
Deck 1 Play
Deck 2 Play
Deck 1 Cue
Deck 2 Cue
Deck 1 PFL
Deck 2 PFL
```

Do not include Sync, loop, pad mode, Smart CFX, or Smart Fader LEDs in this task unless their P4-owned state already exists on current `master`.

- [ ] **Step 3: Request forced snapshot on FLX4 connected**

In `firmware/main-deck-p4/components/control_link/control_link_uart.c`, when receiving:

```text
type = CTRL_TYPE_STATE
id = CTRL_ID_FLX4_CONNECTION
value = CTRL_FLX4_CONNECTED
```

call the snapshot publisher with force enabled. On disconnected, record/log the state but do not change playback, deck state, or UI state.

- [ ] **Step 4: Add snapshot tests**

`tests/flx4_led_snapshot/test_flx4_led_snapshot.c` must assert:

```text
Initial forced snapshot sends all six MVP LED values.
Normal publish suppresses unchanged values after successful send.
Changed Play state sends only the changed Play LED.
Failed send remains dirty and is retried on the next normal publish.
Forced snapshot after reconnect sends all six values, including off values.
```

- [ ] **Step 5: Run P4 host tests and P4 firmware build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
cd D:\Documents\DDJ-FFL4
```

Expected: host tests and P4 build exit `0`.

- [ ] **Step 6: Commit P4 snapshot slice**

Run:

```powershell
git add firmware/main-deck-p4/components/control_link firmware/main-deck-p4/components/deck_core firmware/main-deck-p4/components/ui tests/flx4_led_snapshot tests/run_p4_host_tests.ps1
git commit -m "feat(p4): force FLX4 LED snapshot after reconnect"
```

Expected: commit succeeds.

---

### Task 6: Salvage Smart CFX / Smart Fader input semantics only

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`

- [ ] **Step 1: Add minimal semantic IDs**

Add only these IDs to both S3 and P4 `control_link.h`:

```c
#define CTRL_ID_SMART_CFX       (CTRL_NS_SYSTEM | 0x01)
#define CTRL_ID_SMART_FADER     (CTRL_NS_SYSTEM | 0x02)
```

Use `CTRL_TYPE_BUTTON` for press/release. Do not add P4 DSP enable state, settings UI, audio smart layer, or LED feedback in this task.

- [ ] **Step 2: Map verified raw MIDI messages on S3**

In `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`, under `FLX4_STATUS_GLOBAL_BTN` handling:

```c
if (msg->data1 == 0x00) {
    return emit_button(out, CTRL_ID_SMART_CFX, msg->data2 > 0 ? 1 : 0);
}
if (msg->data1 == 0x01) {
    return emit_button(out, CTRL_ID_SMART_FADER, msg->data2 > 0 ? 1 : 0);
}
```

Keep existing handling for:

```text
0x41 Browse press
0x46 Load Deck 1
0x47 Load Deck 2
```

- [ ] **Step 3: Add S3 mapping tests**

In `tests/flx4_midi_host/test_flx4_map.c`, add:

```c
assert(flx4_map_message(&state, MSG(0x96, 0x00, 0x7F), &ev));
expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_CFX, 1);
assert(flx4_map_message(&state, MSG(0x96, 0x00, 0x00), &ev));
expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_CFX, 0);

assert(flx4_map_message(&state, MSG(0x96, 0x01, 0x7F), &ev));
expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_FADER, 1);
assert(flx4_map_message(&state, MSG(0x96, 0x01, 0x00), &ev));
expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_SMART_FADER, 0);
```

- [ ] **Step 4: Add protocol parity tests**

In `tests/control_link_protocol/test_control_link_protocol.c`, assert:

```c
assert(S3_CTRL_ID_SMART_CFX == P4_CTRL_ID_SMART_CFX);
assert(S3_CTRL_ID_SMART_FADER == P4_CTRL_ID_SMART_FADER);
```

Export the constants from `s3_constants.c` and `p4_constants.c` using the same pattern as existing IDs.

- [ ] **Step 5: Run S3 tests and both firmware builds**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_s3_host_tests.ps1
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
cd D:\Documents\DDJ-FFL4
```

Expected: both host test scripts and both firmware builds exit `0`.

- [ ] **Step 6: Commit Smart input slice**

Run:

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/control-board-s3/components/flx4_midi_host/flx4_map.c tests/flx4_midi_host/test_flx4_map.c tests/control_link_protocol
git commit -m "feat(flx4): map Smart control button inputs"
```

Expected: commit succeeds.

---

### Task 7: End-to-end hardware verification

**Files:**
- Modify after run: `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`
- Modify after run: `docs/validation/FLX4_SMART_INPUT_CAPTURE.md`

- [ ] **Step 1: Flash S3 and P4**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -p COM3 flash monitor
```

In a second terminal:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash monitor
```

Expected:

```text
S3 boots, enumerates DDJ-FLX4, and publishes connected state.
P4 boots, reports control link connected, and requests/sends forced LED snapshot.
```

- [ ] **Step 2: Verify LED reconnect matrix**

Manual acceptance:

```text
Deck 1 Play LED returns after DDJ-FLX4 USB unplug/reinsert while Deck 1 is playing.
Deck 2 Play LED returns after DDJ-FLX4 USB unplug/reinsert while Deck 2 is playing.
Deck 1 Cue/PFL LED state returns while stopped.
Deck 2 Cue/PFL LED state returns while stopped.
Playback state does not change because of reconnect.
Deck state does not change because of reconnect.
```

- [ ] **Step 3: Verify Smart input transport**

Manual acceptance:

```text
Press SMART CFX: S3 emits CTRL_TYPE_BUTTON / CTRL_ID_SMART_CFX / 1.
Release SMART CFX: S3 emits CTRL_TYPE_BUTTON / CTRL_ID_SMART_CFX / 0.
Press SMART FADER: S3 emits CTRL_TYPE_BUTTON / CTRL_ID_SMART_FADER / 1.
Release SMART FADER: S3 emits CTRL_TYPE_BUTTON / CTRL_ID_SMART_FADER / 0.
No P4 audio DSP behavior changes in this slice.
```

- [ ] **Step 4: Record hardware result**

Update the two validation documents with:

```text
Firmware branch: codex/flx4-extended-controls-salvage
S3 commit: <actual short hash after execution>
P4 commit: <actual short hash after execution>
COM ports: S3 COM3, P4 COM15
Result: pass/fail per scenario
Observed logs: one-line summary of S3 connection publication and P4 forced snapshot
```

Replace `<actual short hash after execution>` with the real value from:

```powershell
git rev-parse --short HEAD
```

- [ ] **Step 5: Commit hardware validation update**

Run:

```powershell
git add docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md docs/validation/FLX4_SMART_INPUT_CAPTURE.md
git commit -m "docs: record FLX4 salvage hardware verification"
```

Expected: commit succeeds.

---

### Task 8: Final verification and push

**Files:**
- Modify: none expected.

- [ ] **Step 1: Run full relevant verification**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_s3_host_tests.ps1
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
cd D:\Documents\DDJ-FFL4
git diff --check
git status -sb
```

Expected:

```text
S3 host tests: pass
P4 host tests: pass
S3 idf.py build: pass
P4 idf.py build: pass
git diff --check: exit 0
git status -sb: clean branch
```

- [ ] **Step 2: Push branch**

Run:

```powershell
git push -u origin codex/flx4-extended-controls-salvage
```

Expected: push succeeds.

- [ ] **Step 3: Decide integration path**

Preferred integration:

```text
Fast-forward or PR-review the salvage branch into master only after hardware validation passes.
Do not merge the original codex/flx4-extended-controls branch.
Keep deferred DSP/FX/beat-sync/sampler work as separate future plans.
```

---

## Self-Review

- Spec coverage: the plan covers the user's request to act on the recommendation, avoid direct merge, salvage useful old-branch work, and preserve current master fixes.
- Risk control: the plan explicitly excludes large audio/DSP/UI feature sets that could destabilize current P4 audio scheduling.
- Verification: every firmware/protocol slice has host tests and ESP-IDF build gates; hardware verification is separated from source-level salvage.
- Known gap: Smart CFX and Smart Fader are mapped only as raw semantic button inputs in this pass. Their P4 DSP behavior remains deferred by design.
