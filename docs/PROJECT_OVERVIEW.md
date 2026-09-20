# Pajoniiir Project Overview

Status: **active P4-only overview, reconciled 2026-09-20**.

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

The installed and validated beta image is the immutable annotated `M2` tag at
`d2dabfa7561ff1e0486acc42c7acf42607654e19`, on `ota_0`. The completed feature
was merged into `master` by `d3099f9`; post-merge portability fixes leave
`master` at `c786de7`. The final post-merge GitHub Actions run passed the host,
UI, ESP-IDF v6.0.2 build and binary/provenance gates. The installed M2 image
passed:

- the complete P4 host suite, clean ESP-IDF v6.0.2 signed build and package
  verification;
- signed OTA and exact-image identity verification;
- USB0 mount plus FLX4 profile, MIDI IN/OUT and UAC activation;
- the complete dual-USB lifecycle accounting and reduced OTA recovery matrix;
- real MP3/WAV/FLAC and combined functional acceptance;
- a 180.156-minute combined soak with zero strict counter delta, no reboot or
  TWDT and operator-confirmed clean audio;
- the final physical FLX4, Library, MAIN and cue smoke.

The complete evidence and remaining gates are in
[`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) and
[`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).

## Release boundary

The accelerated M2 beta and merge are complete. Production release `M2.1`
freezes commit `70824d24`. Its exact tagged ESP-IDF v6.0.2 build, signed OTA
installation to `ota_1`, dual-deck counter smoke, operator-confirmed audio/
display/touch smoke, immutable tag and public OTA channel all pass. See
[`validation/M2_1_PRODUCTION_RELEASE_20260920.md`](validation/M2_1_PRODUCTION_RELEASE_20260920.md).

The encrypted offline primary and separately stored encrypted backup signing
key copies are operator-confirmed. A recovery signing test from the backup is
an operational follow-up; it is not claimed as completed release evidence.

The security decision is fixed in
[`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md): WPA2/WPA3
transition mode with PMF capability is enabled; SBOM is waived; Secure Boot,
Flash Encryption and security eFuse burns are deferred on the sole P4 board,
with the physical-access limitation explicitly accepted.

The operator accepted the current enclosure after approximately two months of
use and confirmed a wired recovery path. No separate enclosure rerun is
planned; numeric enclosure thermal/RF margins remain uncaptured.

## Source of truth

- [`ARCHITECTURE.md`](ARCHITECTURE.md)
- [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md)
- [`DDJ_FLX4_MIDI_MAP.md`](DDJ_FLX4_MIDI_MAP.md)
- [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md)
- [`RISK_REGISTER.md`](RISK_REGISTER.md)
- [`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md)
- [`OTA-UPDATE.md`](OTA-UPDATE.md)

Superseded dual-processor plans and evidence are retained under `ARCHIVE_*`,
dated validation records and Git history. They are not active product
instructions.
