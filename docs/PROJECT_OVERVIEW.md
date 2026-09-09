# Pajoniiir Project Overview

Status: **active P4-only overview, updated 2026-09-09**.

Pajoniiir is a standalone dual-deck DJ system. A single ESP32-P4
JC4880P443C_I_W board owns playback, USB, controller state, mixer/DSP, display,
Wi-Fi service and OTA. The Pioneer DDJ-FLX4 is the operator surface; no PC is
required during performance.

## Active topology

- USB0: Rekordbox storage medium.
- USB1: DDJ-FLX4 MIDI input/output and four-channel USB Audio.
- PCM5102A: MAIN/RCA output.
- FLX4 UAC channels 3/4: headphone cue/monitor output.
- P4 local controller runtime: MIDI mapping, semantic-event injection and LED
  resynchronization.
- P4 LVGL application: Overview, Library, Hot Cues and Settings.
- P4 network service: diagnostics, controller-profile update and signed OTA.

There is one firmware target: `firmware/main-deck-p4`. The P4 is the sole
authority for deck position, playback, mixer state, controller binding and LED
decisions.

## Current source and installed baseline

The current source candidate is `RC2-114-gc8b2711` from commit `c8b2711`. It is
pushed, host/build validated and packaged as a verified signed P4 bundle, but it
has not been installed or exercised on hardware.

The latest installed and hardware-tested image is `RC2-113-gaf597d8` from
commit `af597d8`, on `ota_1`. It passed:

- signed OTA and exact-image identity verification;
- USB0 mount plus FLX4 MIDI, MIDI OUT and UAC activation;
- the first remote PLAY after more than two minutes of idle/screensaver;
- a 30-second dual-deck counter window with zero new drop, overflow,
  underflow or output-late events.

The earlier exact candidate also passed a 30-minute dual-active MP3
seek/restart soak. These focused results do not make the branch release-ready.
The complete evidence and remaining gates are in
[`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) and
[`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).

## Release boundary

Before release, complete and record:

1. protected 5 V and independently current-limited downstream VBUS validation;
2. repeated USB0/USB1 hotplug and recovery matrix;
3. real WAV and FLAC load, seek, cache and playback checks;
4. on-device audio deadline, DSP and listening acceptance;
5. guarded web-control and push/pull OTA acceptance;
6. multi-hour dual-deck soak and closed-enclosure thermal/power/RF test;
7. production security decision and final clean exact-commit signed build.

## Source of truth

- [`ARCHITECTURE.md`](ARCHITECTURE.md)
- [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md)
- [`DDJ_FLX4_MIDI_MAP.md`](DDJ_FLX4_MIDI_MAP.md)
- [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md)
- [`RISK_REGISTER.md`](RISK_REGISTER.md)
- [`OTA-UPDATE.md`](OTA-UPDATE.md)

Superseded dual-processor plans and evidence are retained under `ARCHIVE_*`,
dated validation records and Git history. They are not active product
instructions.
