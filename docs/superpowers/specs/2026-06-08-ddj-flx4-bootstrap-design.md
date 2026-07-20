# DDJ-FFL4 Bootstrap Design

Document status (2026-07-13): completed historical bootstrap record. Current
architecture is dual-deck and documented in `../../ARCHITECTURE.md`.

Date: 2026-06-08

## Context

The workspace started as an empty git repository. The project will use
an earlier ESP32 DJ-firmware project as a fork-style technical base. The user supplied a
Croatian project note describing a standalone ESP32-P4 plus ESP32-S3 DDJ-FLX4
system, and supplied a local Mixxx MIDI mapping XML:

```text
E:\Downloads\Pioneer-DDJ-FLX4.midi.xml
```

The XML is vendored into:

```text
docs/reference/Pioneer-DDJ-FLX4.midi.xml
```

## Decision

Use a fork-style port. Import that earlier codebase first, then layer
DDJ-FFL4 documentation and later firmware changes on top.

This is preferred over a clean rewrite because that codebase already proves the
JC4880P443C_I_W P4 board, S3 support firmware, Rekordbox parsing, LVGL UI,
single-deck audio playback, and `0xA5` UART control link.

## Initial Deliverables

- Root `README.md` for DDJ-FFL4.
- Project overview.
- Architecture document.
- DDJ-FLX4 MIDI map derived from the supplied XML.
- Control link extension plan.
- Hardware wiring document.
- Development plan.
- Startup checklist.
- Risk register.
- Vendored reference inputs.

## Architecture

The S3 becomes the USB MIDI host for the DDJ-FLX4. It maps raw MIDI to semantic
events and forwards those events over the inherited UART control link.

The P4 owns authoritative playback state, media loading, display, dual deck
state, audio decode, mixer state, master output, cue/PFL output, and LED
feedback decisions.

## Key Constraint

The FLX4 MIDI XML is not executable firmware logic. It is a mapping reference.
Every MVP control must be verified with raw MIDI capture from actual hardware
before behavior is considered stable.

## MVP Scope

The first implementation milestone should include:

- S3 USB MIDI host raw capture.
- Play, Cue, Load, Jog, Tempo, channel faders, crossfader, and headphone cue.
- Deck-aware `control_link` frames.
- P4 dual deck state.
- Basic two-track master mix.
- Initial Play/Cue/PFL LED feedback.

Out of scope for the first milestone:

- full Mixxx parity;
- beat sync;
- key lock/master tempo;
- four decks;
- full pad modes;
- polished dual waveform UI.

## Self-Review

No placeholders are intentionally left in this design. The scope is limited to
project bootstrap and first-milestone planning; firmware implementation is
covered by the development plan and should proceed phase by phase.
