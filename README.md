# DDJ-FFL4

Standalone dual-deck DJ system built around a Pioneer DDJ-FLX4 controller, an
ESP32-S3 control board, and a JC4880P443C_I_W ESP32-P4 multimedia board.

This repository is a fork-style port of
[`dvucinozd/CDJ100S-XXX`](https://github.com/dvucinozd/CDJ100S-XXX). The
upstream project already proves the hard platform pieces: ESP32-P4 display and
touch, Rekordbox USB library parsing, MP3 preload/decode, ES8311 I2S output,
ESP32-S3 support firmware, and the internal `0xA5` UART control link.

DDJ-FFL4 changes the product target:

- the CDJ-100S front panel is replaced by a Pioneer DDJ-FLX4;
- the ESP32-S3 becomes a USB MIDI host and FLX4 protocol translator;
- the ESP32-P4 becomes a two-deck playback, UI, and mixer engine;
- the existing UART `control_link` remains the semantic event bus between the
  S3 and P4.

## Hardware Roles

| Device | Role |
| --- | --- |
| Pioneer DDJ-FLX4 | Operator surface: transport, jogs, tempo, mixer, cue, pads, LEDs |
| ESP32-S3-DevKitC-1 N16R8 | USB MIDI host for FLX4, MIDI-to-control-link translator, LED feedback bridge |
| JC4880P443C_I_W ESP32-P4 | LVGL UI, Rekordbox media library, two deck states, audio decode, mixer, master/cue output |

## Repository Layout

```text
firmware/
  control-board-s3/   ESP32-S3 firmware inherited from CDJ100S-XXX
  main-deck-p4/       ESP32-P4 firmware inherited from CDJ100S-XXX
docs/
  reference/          vendored upstream and FLX4 mapping inputs
tests/                PC-side test harnesses inherited from CDJ100S-XXX
```

Key project documents:

- [Project overview](docs/PROJECT_OVERVIEW.md)
- [Architecture](docs/ARCHITECTURE.md)
- [DDJ-FLX4 MIDI map](docs/DDJ_FLX4_MIDI_MAP.md)
- [Control link protocol](docs/CONTROL_LINK_PROTOCOL.md)
- [Hardware wiring](docs/HARDWARE_WIRING.md)
- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Startup checklist](docs/STARTUP_CHECKLIST.md)
- [Risk register](docs/RISK_REGISTER.md)

## Reference Inputs

- Original upstream README: [docs/reference/CDJ100S-XXX-README.md](docs/reference/CDJ100S-XXX-README.md)
- Mixxx DDJ-FLX4 MIDI mapping XML:
  [docs/reference/Pioneer-DDJ-FLX4.midi.xml](docs/reference/Pioneer-DDJ-FLX4.midi.xml)
- Existing upstream technical docs remain under [docs/](docs/) and should be
  treated as the baseline implementation reference unless superseded by the
  DDJ-FFL4 documents.

## Build Environment

The inherited firmware expects the same baseline environment as CDJ100S-XXX:

- ESP-IDF v5.5
- Python environment installed by Espressif tooling
- Native MinGW/GCC for PC unit tests on Windows

Typical shell bootstrap:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
```

Inherited build entry points:

```powershell
cd firmware/control-board-s3
idf.py build

cd ..\main-deck-p4
idf.py build
```

P4 host regression tests on Windows:

```powershell
.\tests\run_p4_host_tests.ps1
```

S3 host regression tests on Windows:

```powershell
.\tests\run_s3_host_tests.ps1
```

Do not treat the whole system as DDJ-FLX4-ready yet. The P4 target now has
substantial DDJ-FFL4 work in place: deck-aware state, source-safe dual-deck
library load paths, shared output/mixer audio plumbing, and a refactored
two-deck LVGL UI with Pioneered-style Overview chrome, centered beat/phase
indicators, fixed-segment timers, and bounded title/timer invalidation.
The S3 target has a raw FLX4 USB MIDI logger, a deck-aware software
translator behind `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`, and host tests for the
mapper/protocol path. Physical FLX4 USB enumeration, raw MIDI capture, dual-deck
headphone cue/PFL routing, and active physical FLX4 LED feedback (Play, Cue, PFL)
are fully implemented. The porting steps are tracked in
[docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md).

## MVP Target

The first DDJ-FFL4 milestone is not a full controller clone. It is a stable
two-deck standalone path:

1. S3 enumerates the DDJ-FLX4 as a class-compliant USB MIDI device.
2. S3 translates Play, Cue, Load, Jog, Tempo, channel faders, crossfader, and
   headphone cue into deck-aware `control_link` frames.
3. P4 maintains two independent deck states. Current P4 firmware already does
   this for the local UI/control path.
4. P4 decodes two tracks and outputs a simple master mix with Split Mono and Stereo Master headphone cue/PFL audio routing (implemented).
5. P4 sends transport and mixer LED feedback (Play, Cue, PFL) back to the S3, and S3 sends the matching MIDI LED messages to the FLX4 (implemented).

## Extended Controller Plan

The remaining useful FLX4 controls will use the vendored Mixxx XML for MIDI
status, midino, message encoding, deck/shift channels, and 14-bit pairing. The
MVP hardware capture matched those definitions exactly. Mixxx JavaScript logic
will not run on either ESP32: the S3 emits semantic events and the P4 owns all
deck, mixer, pad-mode, effect, playback, and LED state. Implementation order and
acceptance criteria are defined in Phase 7 of
[docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md).
