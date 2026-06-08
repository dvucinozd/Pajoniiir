# CDJ100S-XXX Project Documentation

Architecture decisions, board analysis, hardware bringup notes, and development plan.

## Analysis & Decisions

- [`source-xdj100sx-analysis.md`](source-xdj100sx-analysis.md) — upstream XDJ100SX project: what it contains, which parts are reusable, MIDI contract
- [`board-jc4880p443c-i-w-analysis.md`](board-jc4880p443c-i-w-analysis.md) — JC4880P443C_I_W board: pin map, peripherals, constraints
- [`external-bsp-analysis.md`](external-bsp-analysis.md) — ESP-IDF BSP candidate analysis
- [`framework-decision.md`](framework-decision.md) — ESP-IDF for production, Arduino for smoke tests
- [`control-board-decision.md`](control-board-decision.md) — why CDJ controls moved to dedicated ESP32-S3
- [`porting-architecture.md`](porting-architecture.md) — two-board architecture, component map, design decisions
- [`rekordbox-format-analysis.md`](rekordbox-format-analysis.md) — ANLZ file format (waveform, BPM, cues, VBR seek); USB drive folder structure

## Implementation

- [`development-plan.md`](development-plan.md) — staged implementation plan (Phase 0–10) with task checklists
- [`cdj-link-cache-preload.md`](cdj-link-cache-preload.md) — remote USB library, binary protocol and SD cache/preload design

## Hardware Bringup

- [`bench-notes.md`](bench-notes.md) — hardware smoke test results (ESP32-S3 + P4 display/touch/USB/audio confirmed)
- [`wiring-map.md`](wiring-map.md) — CDJ front panel → ESP32-S3 GPIO assignments (confirmed)

## Local Inputs (not in git)

- `../upstream/XDJ100SX/` — cloned from `https://github.com/marcmonka/XDJ100SX`
- `../upstream/esp32_p4_jc4880p433c_bsp/` — cloned from `https://github.com/csvke/esp32_p4_jc4880p433c_bsp`
- `../JC4880P443C_I_W/` — board documentation, schematics, examples, user manual
