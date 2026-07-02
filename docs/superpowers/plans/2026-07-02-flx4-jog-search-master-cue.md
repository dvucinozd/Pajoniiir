# FLX4 Jog Search And Master Cue Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement DDJ-FLX4 Jog Search and physical Master Cue while documenting Vinyl mode as intentionally out of scope.

**Architecture:** Add shared semantic IDs, map S3 MIDI input from the official XML/PDF, implement P4-owned behavior in deck_core/audio_engine, and publish a P4-owned Master Cue LED snapshot. Keep S3 stateless except for normal mapper state.

**Tech Stack:** ESP-IDF C firmware, existing `0xA5` control_link protocol, host C tests under `tests/`.

---

### Task 1: S3 mapping and shared protocol

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Test: `tests/flx4_midi_host/test_flx4_map.c`
- Test: `tests/control_link_protocol/test_control_link_protocol.c`

- [x] Write failing tests for Jog Search and Master Cue semantic IDs.
- [x] Run S3/control-link host tests and confirm the failure.
- [x] Add `CTRL_ID_DECK*_JOG_SEARCH`, `CTRL_ID_DECK*_JOG_SEARCH_TOUCH`, and `CTRL_ID_MASTER_CUE`.
- [x] Map `B0/B1 0x29`, `90/91 0x67`, `96 63`, and `96 68`.
- [x] Run host tests and confirm they pass.

### Task 2: P4 Jog Search behavior

**Files:**
- Modify: `firmware/main-deck-p4/components/control_link/control_link_uart.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Test: `tests/deck_core_dual/test_deck_core_dual.c`

- [x] Write failing tests proving Jog Search seeks forward/backward by relative encoder delta and clamps at zero.
- [x] Run P4 host tests and confirm the failure.
- [x] Treat `CTRL_ID_DECK*_JOG_SEARCH` as a deck jog/encoder event.
- [x] Add a dedicated `on_jog_search()` path in deck_core.
- [x] Run P4 host tests and confirm they pass.

### Task 3: Master Cue audio and LED

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c`
- Modify: `firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h`
- Modify: `firmware/control-board-s3/components/control_link/flx4_led_midi.c`
- Test: `tests/audio_engine/test_audio_engine.c`
- Test: `tests/audio_output_mixer/test_audio_output_mixer.c`
- Test: `tests/deck_core_dual/test_deck_core_dual.c`
- Test: `tests/flx4_midi_host/test_flx4_led_midi.c`

- [x] Write failing tests for Master Cue toggle, audio monitor behavior, and LED MIDI OUT.
- [x] Run relevant host tests and confirm the failure.
- [x] Add `audio_engine_toggle_master_cue()` and snapshot state.
- [x] Add `master_cue_enabled` to full output mixer headphone rendering.
- [x] Route `CTRL_ID_MASTER_CUE` press in deck_core and publish `LED_MASTER_CUE`.
- [x] Run host tests and confirm they pass.

### Task 4: Documentation and verification

**Files:**
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [x] Document Jog Search and Master Cue implemented state.
- [x] Document Vinyl as intentionally out of scope.
- [x] Add hardware smoke checklist for Jog Search, Master Cue audio behavior, and Master Cue LED reconnect.
- [x] Run S3 host tests.
- [x] Run P4 host tests.
- [x] Build S3 firmware.
- [x] Build P4 firmware.
- [x] Run `git diff --check` and `git status --short`.
