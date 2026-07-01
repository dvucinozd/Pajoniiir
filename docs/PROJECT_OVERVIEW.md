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

## Current Port Status

The fork is no longer only the imported single-deck baseline:

- S3 raw USB MIDI host logging exists, USB descriptor parsing is hardened, and
  the software translator path can map MVP FLX4 MIDI messages into deck-aware
  `control_link` frames behind `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`.
- Physical DDJ-FLX4 enumeration and raw packet capture were completed on
  2026-06-14. All MVP controls matched the vendored Mixxx XML mapping, and the
  translator is enabled by default.
- Extended controls will use XML status/midino and encoding as implementation
  seeds. Mixxx script callbacks are not runtime logic; the P4 remains
  authoritative for standalone behavior and state.
- The official Pioneer MIDI message list is vendored beside the Mixxx XML and
  is used as the secondary reference for LED outputs and documented conflicts;
  current implementation still treats the XML as the proven input source.
- The DDJ control-link namespace is deck-aware, and the P4 parser carries deck
  and control fields for DDJ events while preserving legacy frames.
- P4 `deck_core` now stores independent Deck 1/Deck 2 state and routes local
  UI operations through deck-aware APIs.
- P4 audio has per-deck engine/ring/resampler/preload/runtime/task context
  storage, a shared output mixer, channel fader/crossfader gain handling,
  deck-local three-band EQ, and Deck 2 producer support. Stereo Master and Split Mono cue/PFL routing are
  implemented for the current output path.
- P4 LVGL UI is dual-deck: Overview, Library load paths, performance target
  selection, Settings, status/header, and waveform rendering are split into
  smaller UI modules.
- Deck 2 lower Overview waveform jitter was fixed on 2026-06-13 by keeping
  Deck 2 on the normal LVGL invalidation/flush path while Deck 1 may use the
  direct overlay path.
- The current Overview UI uses Pioneered-style deck strips, centered beat/phase
  indicators, fixed-segment blue-strip timers, and bounded title/timer
  invalidation so status chrome does not create continuous redraw pressure.
- FLX4 Play/Cue/PFL LED MIDI output is implemented through P4-confirmed
  control-link feedback and the S3 USB MIDI Out queue. P4 also drives selected
  pad-mode LEDs, Beat Sync state LEDs, Loop In/Out LEDs from pending marker and
  active loop state, normal Beat Loop pad LEDs, and VU meter output.
- S3 publishes DDJ-FLX4 USB connection state to P4, and P4 forces a P4-owned
  LED snapshot on reconnect. Hardware verification on 2026-06-20 confirmed
  Play/Cue/PFL LED recovery without playback or deck-state changes; extended
  pad-mode/sync/loop reconnect smoke has covered USB replug and S3 reset, and
  P4-only reset recovery is implemented through S3 heartbeat connected-state
  refresh with hardware smoke passed on 2026-06-26.
- Smart CFX and Smart Fader raw inputs are captured and mapped as momentary
  semantic button events. P4 now owns their state, LED feedback, status
  exposure, Smart CFX filter DSP with softened raw/effective control travel,
  and Smart Fader transition-assist behavior.
- Hot Cue pad behavior is implemented on P4: an empty pad stores the current
  per-track deck position, an occupied pad recalls it with audio seek, and
  Shift + pad clears the slot. Deck 1 hardware behavior smoke passed on
  2026-06-21; Deck 2 behavior uses the same deck-local path and remains marked
  for hardware smoke.
- Phase 7 extended controller work is merged into `master` as of 2026-06-26.
  Implemented P4 behavior now includes Loop In/Out, Reloop/Exit, loop
  halve/double, Beat Jump buttons/pads, normal and shifted Beat Loop pads,
  Tempo Range, Beat Sync BPM-match-on-press with paused-deck phase align, and
  Hot Cue store/recall/clear. Pad FX now has a first P4-owned DSP slice behind
  PAD_FX1/PAD_FX2 `CTRL_PAD_ACTION` events, physical FLX4 Pad FX pad input
  mapping from the official MIDI message PDF, Echo release-tail behavior, and
  host-tested momentary Pad FX pad LED feedback. Pad FX behavior and normal pad
  LED hardware smoke passed on 2026-07-01.
- The stale Codex branches from completed work were cleaned up after the Phase
  7 merge. The only intentionally preserved non-master work is
  `codex/flx4-extended-controls`, an older dirty Smart/DSP experimental
  branch that needs separate review before reuse or deletion.

## Non-Goals For The First Milestone

- Full Mixxx feature parity.
- Continuous beat following, master tempo/key lock, or full Rekordbox-style
  sync state.
- Deeper Beat FX and Pad FX hardware acceptance beyond the current Smart CFX
  filter curve, Smart Fader transition-assist V1 behavior, Beat FX FILTER,
  first fixed-delay Echo slice, and host-tested Pad FX DSP/input/LED slice.
- Four-deck support.
- Rekordbox library editing.
- Running JavaScript Mixxx mappings on-device.
- Treating FLX4 MIDI XML as executable logic.
