# Documentation Status

Status: **active P4-only source of truth, reconciled 2026-09-09**.

## Product boundary

The active product has one firmware and release target: ESP32-P4. The P4 owns
USB0 Rekordbox storage, direct USB1 DDJ-FLX4 MIDI/audio, controller mapping and
LEDs, dual-deck playback, mixer/DSP, MAIN/cue outputs, LVGL UI, Wi-Fi service
and OTA. No secondary processor, inter-board transport or secondary firmware
gate belongs to the active product.

Legacy dual-processor ledgers are retained only in files whose names begin with
`ARCHIVE_`. Dated validation records remain evidence of the image and topology
they actually tested; they are not current instructions.

## Current source candidate

- Branch: `feat/p4-dual-usb-host`
- Commit: `c8b27116d1d261e15357f89ee2928f333b75d291`
- Version: `RC2-114-gc8b2711`
- Application size: `2,452,928` bytes
- Application SHA-256:
  `7cdaf7ec9b5d6b8d386121c0ed9e4b658b182963be86073fadafc9c148ae70e8`
- Signed bundle size: `2,453,116` bytes
- Signed bundle SHA-256:
  `e19815a1c843c64f2f10f420e13906309d779ca18a4fdbc1b39136f34c551510`
- Toolchain: ESP-IDF v6.0.2
- Publication state: pushed; local and remote SHA matched
- Validation state: P4 host/UI/keylock/build/harness gates and signed-package
  verification passed; **not installed or hardware-smoked**

The source candidate closes the review findings recorded in
[`validation/CODE_REVIEW_P4_REMEDIATION_20260906.md`](validation/CODE_REVIEW_P4_REMEDIATION_20260906.md),
including audio worker teardown ownership, nonblocking output bookkeeping,
controller delivery convergence, UAC packet-loss accounting and valid hardware
rate selection.

## Latest installed hardware baseline

- Commit: `af597d8813f8a6a7fc20898bb25b5943230edab1`
- Installed version: `RC2-113-gaf597d8`
- Installed slot: `ota_1`
- Application size: `2,451,840` bytes
- Application SHA-256:
  `e9966017d078dece284ee1e8c7813ea1820ebc65022a618101572641b4d40eca`
- Signed bundle SHA-256:
  `6858a36f714e61926f12f88ad4c3d3b506b9a5fb728f28e3f0c43931a29a3e17`
- Toolchain: ESP-IDF v6.0.2

This exact installed image passed signed OTA, USB0 mount, direct FLX4 profile/MIDI/UAC
startup, first remote PLAY after the 120-second screensaver timeout and a
30-second dual-deck counter window with zero drop, overflow, underflow and
output-late deltas. See
[`validation/P4_REMOTE_PLAY_UAC_HEALTH_OTA_SMOKE_20260902.md`](validation/P4_REMOTE_PLAY_UAC_HEALTH_OTA_SMOKE_20260902.md).

Earlier focused evidence remains valid within its stated limits:

- [`validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md`](validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md)
  — one USB0 remove/reinsert with FLX4 active;
- [`validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md`](validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md)
  — one FLX4 reconnect with USB0 retained and post-reconnect dual playback;
- [`validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md`](validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md)
  — 30-minute dual-active MP3 soak with seven seek/restart cycles.

## Release status

The branch is **not release-qualified and must not be merged yet**. The code,
host tests, signed build and focused product paths are healthy; the critical
path is now hardware qualification:

1. measured, protected and backfeed-free common 5 V/VBUS distribution;
2. complete cold/warm boot, insertion-order and repeated USB0/USB1 recovery
   matrix, including USB0 removal during active load/decode;
3. real MP3/WAV/FLAC bounded-cache acceptance with physically verified files;
4. on-device Master Tempo CPU/I2S deadline and listening-quality acceptance;
5. guarded P4 web, profile and pull/push OTA fault/recovery matrix;
6. multi-hour combined-load soak;
7. closed-enclosure power, thermal, RF and wired-recovery acceptance;
8. production credential, signing-key, rotation and optional irreversible
   security decisions;
9. exact final-candidate full functional smoke, documentation freeze and tag.

The complete ordered handoff is
[`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).
The compact recurring checklist is
[`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md).

## Scope deferred beyond the first FLX4 release

The following are not blockers unless explicitly added to the first release:

- physical acceptance of non-FLX4 controller profiles;
- re-enabling the master-output recorder;
- LIBAPTA integration;
- new UI or controller features outside the current FLX4 scope.

## Source-of-truth order

1. active firmware, tests and build configuration;
2. this status page and `migration/P4_DUAL_USB_NEXT_SESSION.md`;
3. `STARTUP_CHECKLIST.md`, `RISK_REGISTER.md` and
   `fixevi-remediation-audit.md`;
4. dated validation records;
5. `ARCHIVE_*` files and Git history.
