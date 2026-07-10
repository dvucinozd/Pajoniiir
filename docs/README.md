# DDJ-FFL4 Project Documentation

Architecture decisions, board analysis, hardware bring-up notes, and development plans.

The active DDJ-FLX4 port roadmap is [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md).

## Active DDJ-FFL4 Docs

- [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) - product shape and current port status
- [`ARCHITECTURE.md`](ARCHITECTURE.md) - S3/P4 responsibility split
- [`DDJ_FLX4_MIDI_MAP.md`](DDJ_FLX4_MIDI_MAP.md) - FLX4 MVP controls and XML-derived mapping notes
- [`CONTROL_LINK_PROTOCOL.md`](CONTROL_LINK_PROTOCOL.md) - internal UART frame and DDJ control IDs
- [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md) - current two-board wiring notes
- [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) - active staged DDJ-FLX4 plan
- [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md) - current bootstrap/hardware checklist
- [`RISK_REGISTER.md`](RISK_REGISTER.md) - remaining technical risks
- [`bench-notes.md`](bench-notes.md) - current hardware smoke notes and bench results

## Validation Records

- [`validation/FLX4_SMART_INPUT_CAPTURE.md`](validation/FLX4_SMART_INPUT_CAPTURE.md) - physical SMART CFX / SMART FADER MIDI input capture and integration notes
- [`validation/FLX4_LED_MIDI_OUT_CAPTURE.md`](validation/FLX4_LED_MIDI_OUT_CAPTURE.md) - physical LED MIDI output capture and reconnect resynchronization notes
- [`validation/FLX4_USB_AUDIO_E2E_SMOKE.md`](validation/FLX4_USB_AUDIO_E2E_SMOKE.md) - full PCM5102A MAIN + FLX4 USB headphones product smoke and S3 overrun regression notes

## Analysis & Decisions

- [`source-xdj100sx-analysis.md`](source-xdj100sx-analysis.md) — upstream XDJ100SX project: what it contains, which parts are reusable, MIDI contract
- [`board-jc4880p443c-i-w-analysis.md`](board-jc4880p443c-i-w-analysis.md) — JC4880P443C_I_W board: pin map, peripherals, constraints
- [`external-bsp-analysis.md`](external-bsp-analysis.md) — ESP-IDF BSP candidate analysis
- [`framework-decision.md`](framework-decision.md) — ESP-IDF for production, Arduino for smoke tests
- [`control-board-decision.md`](control-board-decision.md) — why CDJ controls moved to dedicated ESP32-S3
- [`porting-architecture.md`](porting-architecture.md) — two-board architecture, component map, design decisions
- [`rekordbox-format-analysis.md`](rekordbox-format-analysis.md) — ANLZ file format (waveform, BPM, cues, VBR seek); USB drive folder structure

## Local Inputs (not in git)

- `../upstream/XDJ100SX/` — cloned from `https://github.com/marcmonka/XDJ100SX`
- `../upstream/esp32_p4_jc4880p433c_bsp/` — cloned from `https://github.com/csvke/esp32_p4_jc4880p433c_bsp`
- `../JC4880P443C_I_W/` — board documentation, schematics, examples, user manual
