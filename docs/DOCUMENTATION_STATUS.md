# Documentation Status

Status: **active P4-only source of truth, reconciled 2026-09-11**.

## Product boundary

The active product has one firmware and release target: ESP32-P4. The P4 owns
USB0 Rekordbox storage, direct USB1 DDJ-FLX4 MIDI/audio, controller mapping and
LEDs, dual-deck playback, mixer/DSP, MAIN/cue outputs, LVGL UI, Wi-Fi service
and OTA. No secondary processor, inter-board transport or secondary firmware
gate belongs to the active product.

Legacy dual-processor ledgers are retained only in files whose names begin with
`ARCHIVE_`. Dated validation records remain evidence of the image and topology
they actually tested; they are not current instructions.

## Current validated firmware checkpoint

- Branch: `feat/p4-dual-usb-host`
- Commit: `77d723c8d19b1a859b9f5b4fa8421928250c68d3`
- Version: `RC2-116-g77d723c`
- Application size: `2,452,672` bytes
- Application SHA-256:
  `ead88e980b12c06be8f8655cd1018a5671525f07c4c4a5e21b58cfb303a62d19`
- Signed bundle size: `2,452,860` bytes
- Signed bundle SHA-256:
  `753b22ca2f3276786897f8fd48401fd9c397084489bfdbcc9eb808452170fb3d`
- Toolchain: ESP-IDF v6.0.2
- Publication state: pushed; local and remote SHA matched before this
  documentation-only successor
- Validation state: P4 host/build and signed-package verification passed;
  installed by signed OTA and targeted three-hour limiter/WDT hardware soak
  passed

This firmware checkpoint retains the review remediations recorded in
[`validation/CODE_REVIEW_P4_REMEDIATION_20260906.md`](validation/CODE_REVIEW_P4_REMEDIATION_20260906.md),
including audio worker teardown ownership, nonblocking output bookkeeping,
controller delivery convergence, UAC packet-loss accounting and valid hardware
rate selection. It also removes the limiter-telemetry lock cycle that caused
the earlier audio-task watchdog reset. Later documentation-only commits do not
alter this installed binary or its exact-image evidence.

## Latest installed hardware baseline

- Commit: `77d723c8d19b1a859b9f5b4fa8421928250c68d3`
- Installed version: `RC2-116-g77d723c`
- Installed slot: `ota_1`
- Application size: `2,452,672` bytes
- Application SHA-256:
  `ead88e980b12c06be8f8655cd1018a5671525f07c4c4a5e21b58cfb303a62d19`
- Signed bundle size: `2,452,860` bytes
- Signed bundle SHA-256:
  `753b22ca2f3276786897f8fd48401fd9c397084489bfdbcc9eb808452170fb3d`
- Toolchain: ESP-IDF v6.0.2

This exact installed image passed signed OTA, USB0 mount, direct FLX4
profile/MIDI/UAC startup and a targeted three-hour continuous dual-MP3
limiter/WDT soak. Boot epoch `389` remained unchanged, with no watchdog reset,
PCM underrun or active UAC loss and no observable output failure, controller
disconnect or USB host daemon error. Fourteen output-late warnings over
1,999,090 submitted UAC blocks were investigated and did not correlate with a
downstream failure; no source change is justified from that count alone. See
[`validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md`](validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md).

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
