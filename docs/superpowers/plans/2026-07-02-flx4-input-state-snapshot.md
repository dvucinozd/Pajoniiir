# FLX4 Input State Snapshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replay known absolute DDJ-FLX4 input values from S3 to P4 after reconnect/heartbeat refresh so P4 applies current mixer/audio control state without requiring knob/fader movement.

**Architecture:** Extend `flx4_map_state_t` with snapshot visibility for scoped absolute controls, expose a small iterator API, and call it from the S3 translator after successful FLX4 connection refresh. Reuse existing `CTRL_TYPE_PITCH` semantic IDs on P4; do not add new protocol frame types in this phase.

**Tech Stack:** ESP-IDF C firmware, existing `control_link` 7-byte UART protocol, host C tests under `tests/flx4_midi_host`.

---

### Task 1: Snapshot mapper API

**Files:**
- Modify: `firmware/control-board-s3/components/flx4_midi_host/include/flx4_map.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Test: `tests/flx4_midi_host/test_flx4_map.c`

- [ ] **Step 1: Write failing host tests**

Add tests that feed MSB/LSB pairs for CH1 trim, crossfader, master volume, headphones mix, and Beat FX depth, then assert snapshot iteration returns those known values.

Add tests that feed tempo and button events, then assert they are not emitted through the snapshot iterator.

- [ ] **Step 2: Verify RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\flx4_midi_host
```

Expected before implementation: compile failure for the missing snapshot API or assertion failure for missing snapshot behavior.

- [ ] **Step 3: Implement mapper snapshot API**

Add:

```c
typedef bool (*flx4_map_snapshot_emit_cb_t)(uint8_t type, uint8_t id, int16_t value, void *ctx);
size_t flx4_map_emit_snapshot(const flx4_map_state_t *state,
                              flx4_map_snapshot_emit_cb_t cb,
                              void *ctx);
```

The iterator emits only fully-known scoped absolute controls. Beat FX depth uses its own known flag and 7-bit value.

- [ ] **Step 4: Verify GREEN**

Run the same host test command and confirm it passes.

### Task 2: S3 heartbeat/reconnect replay

**Files:**
- Modify: `firmware/control-board-s3/main/app_main.c`
- Test: `tests/flx4_midi_host/test_flx4_map.c`

- [ ] **Step 1: Add replay helper**

Add an S3 helper that calls `flx4_map_emit_snapshot()` and sends each emitted event through `control_link_send_semantic()`.

- [ ] **Step 2: Hook replay after connection refresh**

In `heartbeat_task()`, after `flx4_midi_host_refresh_connection_state()` returns true, call the snapshot replay helper.

- [ ] **Step 3: Avoid noisy logs**

Only log replay summary at debug level or not at all. Do not emit one log line per control.

- [ ] **Step 4: Verify S3 build**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
```

Expected: build exits 0.

### Task 3: Documentation

**Files:**
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/DEVELOPMENT_PLAN.md` or `docs/STARTUP_CHECKLIST.md` if phase/checklist text needs it

- [ ] **Step 1: Document behavior**

Document that S3 now replays known absolute input state after FLX4 connection refresh, but does not query unknown physical values from FLX4.

- [ ] **Step 2: Document exclusions**

Explicitly note that tempo faders and buttons/toggles are excluded from this phase.

- [ ] **Step 3: Add hardware smoke checklist**

Add a later smoke item: move master/trim/EQ/filter/faders, reboot P4 while S3/FLX4 stay powered, confirm P4 reapplies the cached values without further movement.

### Task 4: Final verification and commit

**Files:**
- All touched files

- [ ] **Step 1: Run host tests**

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\flx4_midi_host
```

- [ ] **Step 2: Run S3 build**

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
```

- [ ] **Step 3: Run diff checks**

```powershell
git diff --check
git status --short
```

- [ ] **Step 4: Commit and push**

```powershell
git add docs firmware tests
git commit -m "feat: replay flx4 input state snapshot"
git push
```
