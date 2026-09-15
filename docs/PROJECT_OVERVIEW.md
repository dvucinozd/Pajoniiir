# Pajoniiir Project Overview

Status: **active P4-only overview, updated 2026-09-11**.

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

The current validated firmware and latest installed hardware candidate is
`RC2-116-g77d723c` from commit `77d723c`, on `ota_1`. It passed:

- the complete P4 host suite, clean ESP-IDF v6.0.2 signed build and package
  verification;
- signed OTA and exact-image identity verification;
- USB0 mount plus FLX4 profile, MIDI IN/OUT and UAC activation;
- a targeted three-hour continuous dual-MP3 limiter/WDT soak with one boot
  epoch and no watchdog reset, PCM underrun or active UAC loss and no
  observable USB/controller/output failure.

The run recorded 14 rare output-late warnings over 1,999,090 submitted UAC
blocks. The worst was 12,169 us against the deliberately sensitive 11,610 us
warning threshold, with no downstream failure. Inspection points to bounded
I2S pacing/scheduler jitter rather than a limiter or DSP defect, so no code
change was made. An earlier exact candidate also passed a 30-minute dual-active
MP3 seek/restart soak. These focused results do not make the branch
release-ready.
The complete evidence and remaining gates are in
[`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) and
[`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).

## Release boundary

Before release, complete and record:

1. repeated USB0/USB1 hotplug and recovery matrix;
2. real WAV and FLAC load, seek, cache and playback checks;
3. on-device audio deadline, DSP and listening acceptance;
4. guarded web-control and push/pull OTA acceptance;
5. multi-hour combined-load soak;
6. closed-enclosure thermal/power/RF test, including repetition of the passed
   bench 5 V/VBUS measurements;
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
