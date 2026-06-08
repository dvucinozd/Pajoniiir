# DDJ-FFL4 Project Overview

## Goal

DDJ-FFL4 is a standalone two-deck DJ player using the Pioneer DDJ-FLX4 as the
operator surface. It does not run Mixxx and it does not require a PC during
performance. The DDJ-FLX4 supplies controls and LEDs; the ESP32 boards provide
media browsing, playback, audio output, and display.

The project starts as a fork-style port of CDJ100S-XXX because that codebase
already proves the most important hardware and firmware surfaces on the target
boards.

## Product Shape

The system is split into two firmware targets:

- `firmware/control-board-s3`: USB MIDI host and protocol translator.
- `firmware/main-deck-p4`: media library, dual deck engine, UI, audio mixer.

The S3 should stay simple. It reads raw FLX4 MIDI input, maps it to semantic
events, forwards those events over UART, and mirrors P4 LED feedback back to
the FLX4. It must not own playback state.

The P4 owns all authoritative deck state. It loads Rekordbox media from USB,
tracks current position, controls audio decode, drives the local display, and
decides which LEDs should be on.

## Baseline Inherited From CDJ100S-XXX

The imported upstream code already provides:

- ESP32-P4 JC4880 board support for ST7701S display, GT911 touch, ES8311 audio,
  USB mass storage, and SD card cache/config.
- Rekordbox `export.pdb` and `ANLZ0000.DAT` parsing.
- A single-deck `deck_core` state machine.
- A single-track `audio_engine` with MP3 preload, minimp3 decode, pitch
  resampling, instant seek, hot cue, beat jump, and loop support.
- An ESP32-S3 firmware target with UART `control_link`.
- PC unit tests for parsers, audio engine, control link, and selected UI logic.

## New DDJ-FFL4 Work

The fork must add:

- USB MIDI host input on the S3 for the DDJ-FLX4.
- A verified FLX4 mapping table based on
  `docs/reference/Pioneer-DDJ-FLX4.midi.xml` and bench MIDI capture.
- Deck-aware `control_link` event IDs or values.
- Dual `deck_core` instances on the P4.
- Dual audio decode and a mixer/output task.
- Master and cue/PFL audio routing.
- FLX4 LED feedback from P4 state.
- Dual-deck LVGL UI changes after the engine path is stable.

## Non-Goals For The First Milestone

- Full Mixxx feature parity.
- Beat sync or master tempo/key lock.
- Four-deck support.
- Rekordbox library editing.
- Running JavaScript Mixxx mappings on-device.
- Treating FLX4 MIDI XML as executable logic.
