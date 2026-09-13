# Documentation Status

Status: **active P4-only source of truth, reconciled 2026-09-13**.

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
- Commit: `06c0e858ec77b0264b21566ee726e2d2135f365f`
- Version: `RC2-127-g06c0e85`
- Application size: `2,454,848` bytes
- Application SHA-256:
  `9ad6149bf48605ad6b25b76f097e53c82bf1cca6c7f3d7fe9fc6f76c36875cdc`
- Signed bundle size: `2,455,036` bytes
- Signed bundle SHA-256:
  `2aa8162ff522905e3e055d40656f5ab38c4df4942c17e5d3ec5d9bdb2688a9a2`
- Toolchain: ESP-IDF v6.0.2
- Publication state: pushed; local and remote SHA matched before this
  documentation-only successor
- Validation state: P4 host/build and signed-package verification passed;
  installed by signed OTA, exact-image smoke passed and lifecycle Groups
  A--F pass; the earlier `RC2-116-g77d723c` targeted three-hour limiter/WDT
  hardware soak remains valid within its scope

This firmware checkpoint retains the review remediations recorded in
[`validation/CODE_REVIEW_P4_REMEDIATION_20260906.md`](validation/CODE_REVIEW_P4_REMEDIATION_20260906.md),
including audio worker teardown ownership, nonblocking output bookkeeping,
controller delivery convergence, UAC packet-loss accounting and valid hardware
rate selection. It also includes the bounded PDB reader, fail-closed partial
catalog handling and guarded deterministic Library-load validation barrier.

## Latest installed hardware baseline

- Commit: `06c0e858ec77b0264b21566ee726e2d2135f365f`
- Installed version: `RC2-127-g06c0e85`
- Installed slot: `ota_0`
- Application size: `2,454,848` bytes
- Application SHA-256:
  `9ad6149bf48605ad6b25b76f097e53c82bf1cca6c7f3d7fe9fc6f76c36875cdc`
- Signed bundle size: `2,455,036` bytes
- Signed bundle SHA-256:
  `2aa8162ff522905e3e055d40656f5ab38c4df4942c17e5d3ec5d9bdb2688a9a2`
- Toolchain: ESP-IDF v6.0.2

This exact installed image passed signed OTA, USB0 mount, direct FLX4
profile/MIDI/UAC startup and all five deterministic Library-load removal
cycles on boot epoch 415. Groups A--F now account for 26/50 accepted lifecycle
cycles. The earlier `RC2-116-g77d723c` remained on boot epoch 389 through its
targeted three-hour continuous dual-MP3 limiter/WDT soak, with no watchdog
reset, PCM underrun or active UAC loss and no observable output failure,
controller disconnect or USB host daemon error. Fourteen output-late warnings
over 1,999,090 submitted UAC blocks did not correlate with a downstream
failure; no source change is justified from that count alone. See
[`validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md`](validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md).

The working tree also contains deterministic Group G audio-load removal
support. Its guarded loader gate pauses after the first bounded 32 KiB cache
read, and the updated lifecycle harness alternates the target deck across
G1--G5. The gate unit test, harness self-test, complete P4 host suite and
ESP-IDF v6.0.2 build pass. This dirty build is not installed and provides no
Group G hardware evidence until committed, signed, installed and physically
executed.

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

1. complete cold/warm boot, insertion-order and repeated USB0/USB1 recovery
   matrix, including USB0 removal during active load/decode;
2. real MP3/WAV/FLAC bounded-cache acceptance with physically verified files;
3. on-device Master Tempo CPU/I2S deadline and listening-quality acceptance;
4. guarded P4 web, profile and pull/push OTA fault/recovery matrix;
5. multi-hour combined-load soak;
6. closed-enclosure power, thermal, RF and wired-recovery acceptance, including
   repetition of the passed bench electrical measurements;
7. production credential, signing-key, rotation and optional irreversible
   security decisions;
8. exact final-candidate full functional smoke, documentation freeze and tag.

The operator-confirmed common 5 V and dual-VBUS bench acceptance is recorded in
[`validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md`](validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md).
Raw numeric readings were not preserved, so the result applies only to the
unchanged current bench wiring and must be repeated in the final enclosure.

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
