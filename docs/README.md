# DDJ-FFL4 Project Documentation

Architecture decisions, board analysis, hardware bring-up notes, and development plans.

The active DDJ-FLX4 port roadmap is [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md).
The older [`development-plan.md`](development-plan.md) remains as historical
CDJ100S/P4 standalone context from the imported project.

## Active DDJ-FFL4 Docs

- [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) - product shape and current port status
- [`ARCHITECTURE.md`](ARCHITECTURE.md) - S3/P4 responsibility split
- [`DDJ_FLX4_MIDI_MAP.md`](DDJ_FLX4_MIDI_MAP.md) - FLX4 MVP controls and XML-derived mapping notes
- [`CONTROL_LINK_PROTOCOL.md`](CONTROL_LINK_PROTOCOL.md) - internal UART frame and DDJ control IDs
- [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md) - current two-board wiring notes
- [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) - active staged DDJ-FLX4 plan
- [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md) - current bootstrap/hardware checklist
- [`RISK_REGISTER.md`](RISK_REGISTER.md) - remaining technical risks

## Analysis & Decisions

- [`source-xdj100sx-analysis.md`](source-xdj100sx-analysis.md) — upstream XDJ100SX project: what it contains, which parts are reusable, MIDI contract
- [`board-jc4880p443c-i-w-analysis.md`](board-jc4880p443c-i-w-analysis.md) — JC4880P443C_I_W board: pin map, peripherals, constraints
- [`external-bsp-analysis.md`](external-bsp-analysis.md) — ESP-IDF BSP candidate analysis
- [`framework-decision.md`](framework-decision.md) — ESP-IDF for production, Arduino for smoke tests
- [`control-board-decision.md`](control-board-decision.md) — why CDJ controls moved to dedicated ESP32-S3
- [`porting-architecture.md`](porting-architecture.md) — two-board architecture, component map, design decisions
- [`rekordbox-format-analysis.md`](rekordbox-format-analysis.md) — ANLZ file format (waveform, BPM, cues, VBR seek); USB drive folder structure

## Implementation

- [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) - active DDJ-FLX4 staged implementation plan
- [`development-plan.md`](development-plan.md) - legacy CDJ100S/P4 standalone plan retained for reference
- [`cdj-link-cache-preload.md`](cdj-link-cache-preload.md) — remote USB library, binary protocol and SD cache/preload design

## Hardware Bringup

- [`bench-notes.md`](bench-notes.md) — hardware smoke test results (ESP32-S3 + P4 display/touch/USB/audio confirmed)
- [`wiring-map.md`](wiring-map.md) — CDJ front panel → ESP32-S3 GPIO assignments (confirmed)

## Local Inputs (not in git)

- `../upstream/XDJ100SX/` — cloned from `https://github.com/marcmonka/XDJ100SX`
- `../upstream/esp32_p4_jc4880p433c_bsp/` — cloned from `https://github.com/csvke/esp32_p4_jc4880p433c_bsp`
- `../JC4880P443C_I_W/` — board documentation, schematics, examples, user manual
