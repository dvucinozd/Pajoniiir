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
- Official Pioneer DDJ-FLX4 MIDI message list:
  [docs/reference/DDJ-FLX4_MIDI_message_List.md](docs/reference/DDJ-FLX4_MIDI_message_List.md)
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

Current `master` is the integration branch for the Phase 7 extended-control
work. The former `codex/phase7-extended-controls-vu` and `codex/splash-screen`
scopes have been merged, verified, pushed, and the stale remote/local branches
were removed on 2026-06-26. One old experimental branch,
`codex/flx4-extended-controls`, is intentionally still preserved outside
`master` because it contains dirty Smart/DSP work that needs a separate review
before any merge/delete decision.

Do not treat the whole system as DDJ-FLX4-ready yet. The P4 target now has
substantial DDJ-FFL4 work in place: deck-aware state, source-safe dual-deck
library load paths, shared output/mixer audio plumbing, hardware-verified
dual-deck audio scheduling/waveform stability, and a refactored two-deck LVGL
UI with Pioneered-style Overview chrome, centered beat/phase indicators,
fixed-segment timers, bounded title/timer invalidation, and calibrated audio
output diagnostics that no longer report normal codec-write pacing as late
blocks. PCM5102A MAIN OUT bring-up is implemented behind the local
`CONFIG_BSP_PCM5102A_MAIN_OUT` build option and hardware-smoked on the
photographed PCM5102MK/PCM5102A board; both RCA line out and the board's 3.5 mm
output were confirmed on 2026-06-30. The external DAC I2S clock is reconfigured
to the loaded track sample rate, ES8311 remains the monitor/speaker path, audio
tasks are pinned away from the LVGL core, and a non-boosting master trim control
is available in Settings for clipping/limiter tuning and is persisted in NVS.
Limiter telemetry is also exposed through the mixer snapshot and central
diagnostics snapshot; `/api/status` mirrors diagnostics for smoke captures, and
the P4 status indicator shows `CLIP n` only when new limiting occurs. P4 boot now shows a short LVGL splash screen (`PajoNiiiR` in the
Musieer font) before loading the main dual-deck UI.
The S3 target has a raw FLX4 USB MIDI logger, a deck-aware software
translator behind `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`, and host tests for the
mapper/protocol path. Physical FLX4 USB enumeration, raw MIDI capture,
Browse/Load routing, dual-deck headphone cue/PFL routing, active physical FLX4
LED feedback (Play, Cue, PFL), FLX4 reconnect LED resynchronization, and raw
Smart CFX/Smart Fader input mapping and P4-owned state/LED behavior are
implemented. Smart CFX enables the deck-local filter DSP from the FLX4 filter
knobs; Smart Fader applies a conservative crossfader transition-assist curve.
The S3 USB MIDI host now
treats FLX4 VU output as low-priority feedback and suppresses raw MIDI INFO log
floods during normal translator operation, preserving controller responsiveness
under dual-deck playback. Extended deck, loop, mixer, monitoring, pad-mode,
pad-action, and VU-meter input/output mapping is implemented and smoke-verified
where documented in `docs/DDJ_FLX4_MIDI_MAP.md`. Loop In/Out, Reloop/Exit, loop
halve/double, Beat Jump, normal/shifted Beat Loop pads, Tempo Range, Beat Sync
BPM-match-on-press with paused-deck phase align, and Hot Cue pad
store/recall/clear now have P4 behavior. Three-band channel EQ is implemented
in the P4 audio path for both decks using the verified FLX4 14-bit EQ controls.
The P4 firmware default is now a
performance-optimized build so dual-deck audio/UI work runs with adequate
headroom; LVGL examples/demos are disabled in `sdkconfig.defaults`. Sampler,
pad FX and deeper Beat FX behavior remain P4 feature work, not S3 mapping work.
The porting
steps are tracked in
[docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md).

## MVP Target

The first DDJ-FFL4 milestone is not a full controller clone. It is a stable
two-deck standalone path:

1. S3 enumerates the DDJ-FLX4 as a class-compliant USB MIDI device.
2. S3 translates Play, Cue, Load, Jog, Tempo, channel faders, crossfader, and
   headphone cue into deck-aware `control_link` frames.
3. P4 maintains two independent deck states. Current P4 firmware already does
   this for the local UI/control path.
4. P4 decodes two tracks and outputs a simple master mix with Split Mono and Stereo Master headphone cue/PFL audio routing (implemented and hardware-verified for dual-deck playback).
5. P4 sends transport, mixer, pad-mode, Beat Sync, and Loop In/Out LED feedback
   (Play, Cue, PFL, selected pad mode, sync-enabled state, pending Loop In
   marker, and active loop indicators) back to the S3, and S3 sends the matching MIDI LED messages to
   the FLX4 (implemented; pad-mode, Beat Sync, and Loop In/Out LED smoke has
   passed where recorded in the validation notes; extended reconnect smoke is
   still pending).
6. S3 publishes FLX4 USB connection state and P4 forces a P4-owned MVP LED
   snapshot after reconnect so Play/Cue/PFL LEDs recover without changing
   playback state. S3 also refreshes the already-connected FLX4 state after
   heartbeat so a P4-only reboot can recover without physically replugging the
   controller (implemented; hardware smoke passed 2026-06-26).

## Extended Controller Plan

The remaining useful FLX4 controls use the vendored Mixxx XML for MIDI status,
midino, message encoding, deck/shift channels, and 14-bit pairing, with the
official Pioneer MIDI message list used as an additional reference for output
LEDs and XML/official-list conflicts. The XML mapping is accepted as the
authoritative source for remaining input controls due to its 100% accuracy in
our hardware tests. Pre-implementation physical capture is bypassed to speed up
development; S3 emits semantic events and P4 owns all deck, mixer, pad-mode,
effect, playback, and LED state directly from the XML seed. Implementation
order and acceptance criteria are defined in Phase 7 of
[docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md).
