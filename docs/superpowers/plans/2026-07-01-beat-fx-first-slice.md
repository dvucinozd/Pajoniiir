# Beat FX First Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first safe Beat FX behavior slice for DDJ-FLX4 controls without introducing delay-buffer DSP or relying on unverified Pad FX pad addresses.

**Architecture:** S3 maps the documented Beat FX controls into new `0xA5` control-link IDs. P4 owns Beat FX state in `deck_core` and exposes host-testable state transitions. The first slice is stateful only plus conservative target/depth/on/off handling; actual echo/delay DSP remains deferred.

**Tech Stack:** ESP-IDF v5.5, C, existing `control_link`, `flx4_map`, `deck_core`, host GCC regression tests.

---

### Task 1: Add Beat FX protocol IDs

**Files:**
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`

- [x] Add system-level Beat FX IDs under `CTRL_NS_SYSTEM`: select previous/next, beat decrement/increment, target, depth, on/off, clear.
- [x] Keep values below the existing frame limit and avoid changing existing IDs.

### Task 2: TDD Beat FX state in P4 deck core

**Files:**
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `firmware/main-deck-p4/components/deck_core/include/deck_core.h`

- [x] Add failing tests for default Beat FX state.
- [x] Add failing tests for select next/prev, beat inc/dec, target CH1/CH2, depth, on/off, and clear.
- [x] Implement minimal Beat FX state and `deck_core_test_get_beat_fx_state()` test accessor.

### Task 3: TDD S3 Beat FX mapping

**Files:**
- Modify: `tests/flx4_midi_host/test_flx4_map.c`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`

- [x] Add failing tests for the documented Beat FX button/CC addresses.
- [x] Map:
  - select next/prev: `0x94/0x63`, `0x94/0x64`
  - beat left/right: `0x94/0x4A`, `0x94/0x4B`
  - channel select CH1/CH2: `0x94/0x10`, `0x95/0x11`
  - level/depth: `0xB4/0x02`
  - on/off: `0x94/0x47`, `0x95/0x47`
  - shift clear: `0x94/0x43`, `0x95/0x43`

### Task 4: Docs and verification

**Files:**
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [x] Mark Beat FX first-slice state/mapping implemented, DSP echo deferred, hardware smoke pending.
- [x] Run `tests/run_p4_host_tests.ps1`.
- [x] Run S3 host tests.
- [x] Build P4 and S3 if both sides changed.
