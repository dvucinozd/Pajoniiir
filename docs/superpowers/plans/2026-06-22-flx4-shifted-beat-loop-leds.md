# FLX4 Shifted Beat Loop Pads and LEDs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement shifted Beat Loop momentary loop-roll behavior and Beat Loop pad LED feedback.

**Architecture:** Keep behavior P4-owned in `deck_core`; reuse the existing Beat Loop length table and audio loop API. Extend the shared LED ID namespace without changing the 0xA5 frame format, then map the new IDs to FLX4 MIDI notes on S3. Extend `flx4_led_snapshot` so Beat Loop pad LEDs are derived from P4 loop state and pad mode.

**Tech Stack:** ESP-IDF C components, existing control-link LED transport, Windows GCC host tests, P4/S3 `idf.py build`.

---

## Task 1: Add Shared Beat Loop Pad LED IDs and S3 MIDI Mapping

**Files:**
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/control_link/flx4_led_midi.c`
- Modify: `tests/flx4_midi_host/test_flx4_led_midi.c`

- [ ] Add LED IDs `LED_BEAT_LOOP_PAD_1` through `LED_BEAT_LOOP_PAD_8` before `LED_REMOTE_COUNT`.
- [ ] Add S3 host tests expecting Deck 1 pad 1 -> `0x97/0x60`, Deck 1 pad 8 -> `0x97/0x67`, Deck 2 pad 1 -> `0x99/0x60`, Deck 2 pad 8 -> `0x99/0x67`.
- [ ] Run `.\tests\run_s3_host_tests.ps1` and verify RED.
- [ ] Implement `flx4_led_midi_build_packet()` support for the new LED IDs.
- [ ] Run `.\tests\run_s3_host_tests.ps1` and verify GREEN.
- [ ] Commit as `feat(s3): map flx4 beat loop pad leds`.

## Task 2: Add P4 Beat Loop Pad LED Snapshot Logic

**Files:**
- Modify: `firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h`
- Modify: `firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/flx4_led_snapshot/test_flx4_led_snapshot.c`

- [ ] Extend snapshot input with `loop_start_ms[2]` and `loop_end_ms[2]`.
- [ ] Add P4 host tests for Beat Loop pad LED matching and off states.
- [ ] Run `.\tests\run_p4_host_tests.ps1` and verify RED.
- [ ] Extend publisher arrays and LED list to include eight Beat Loop pad LEDs.
- [ ] Add loop duration matching using the same Beat Loop pad length table semantics at 120 BPM-compatible millisecond durations.
- [ ] Fill loop start/end in `deck_core` LED snapshot input.
- [ ] Run `.\tests\run_p4_host_tests.ps1` and verify GREEN.
- [ ] Commit as `feat(p4): publish beat loop pad leds`.

## Task 3: Add Shifted Beat Loop Momentary Behavior

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] Add deck_core tests for shifted press/release restore and shifted press/release clear.
- [ ] Run `.\tests\run_p4_host_tests.ps1` and verify RED.
- [ ] Add per-deck shifted loop-roll shadow state.
- [ ] On shifted Beat Loop pad press, snapshot previous loop and set temporary loop.
- [ ] On shifted Beat Loop pad release, restore previous loop or clear loop.
- [ ] Run `.\tests\run_p4_host_tests.ps1` and verify GREEN.
- [ ] Commit as `feat(deck): implement shifted beat loop pads`.

## Task 4: Build, Docs, and Push

**Files:**
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] Run P4 build.
- [ ] Run S3 build.
- [ ] Update docs to mark shifted Beat Loop pad behavior and Beat Loop pad LEDs implemented with hardware smoke pending.
- [ ] Run `git diff --check` and `git status --short`.
- [ ] Commit docs as `docs: record shifted beat loop leds`.
- [ ] Push `codex/phase7-extended-controls-vu`.

## Self-Review Notes

- Hardware smoke remains pending.
- Shifted mirror LED notes remain out of scope.
- No S3 input mapping changes are expected.
- 0xA5 frame format remains unchanged.
