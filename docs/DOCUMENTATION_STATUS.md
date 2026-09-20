# Documentation Status

Status: **active P4-only source of truth, reconciled 2026-09-20**.

## Product boundary

The active product has one firmware and release target: ESP32-P4. The P4 owns
USB0 Rekordbox storage, direct USB1 DDJ-FLX4 MIDI/audio, controller mapping and
LEDs, dual-deck playback, mixer/DSP, MAIN/cue outputs, LVGL UI, Wi-Fi service
and OTA. No secondary processor, inter-board transport or secondary firmware
gate belongs to the active product.

Legacy dual-processor ledgers are retained only in files whose names begin with
`ARCHIVE_`. Dated validation records remain evidence of the image and topology
they actually tested; they are not current instructions.

## Current candidate checkpoint

- Branch: `feat/p4-dual-usb-host`
- Release commit and annotated tag: `d2dabfa7561ff1e0486acc42c7acf42607654e19`
  / `M2`
- Installed build version and slot: `M2` / `ota_0`, boot identity `14`
- Application size: `2,459,520` bytes
- Application SHA-256:
  `4216867d72c4a76f37cc04a5c3b3cf067e08bb9602be8bbd9a8282fe5804dacd`
- Signed bundle size: `2,459,708` bytes
- Signed bundle SHA-256:
  `f5620858e9983f8272eceb4d3dc93afee7b906cc6e8335e8280b1ceed5bcf9a5`
- Toolchain: ESP-IDF v6.0.2
- Publication state: public `latest.json` and versioned `M2` bundle verified;
  annotated `M2` tag identifies the exact installed source commit
- Validation state: P4 host/build and signed-package verification passed;
  installed by signed OTA and exact-image smoke passed; focused real-file
  MP3/WAV/FLAC cache/playback and audible mixed-format checks pass; lifecycle
  Groups A--H, K and L pass, I1/I2/J1 pass and seven remaining I/J cycles are
  explicitly waived; the earlier
  `RC2-116-g77d723c` targeted three-hour limiter/WDT
  hardware soak remains valid within its scope

This firmware checkpoint retains the review remediations recorded in
[`validation/CODE_REVIEW_P4_REMEDIATION_20260906.md`](validation/CODE_REVIEW_P4_REMEDIATION_20260906.md),
including audio worker teardown ownership, nonblocking output bookkeeping,
controller delivery convergence, UAC packet-loss accounting and valid hardware
rate selection. It also includes the bounded PDB reader, fail-closed partial
catalog handling and guarded deterministic Library-load validation barrier.

## Latest installed hardware candidate

- Source commit and tag: `d2dabfa7561ff1e0486acc42c7acf42607654e19` / `M2`
- Installed version: `M2`
- Installed slot and boot identity: `ota_0` / `14`
- Application size: `2,459,520` bytes
- Application SHA-256:
  `4216867d72c4a76f37cc04a5c3b3cf067e08bb9602be8bbd9a8282fe5804dacd`
- Signed bundle size: `2,459,708` bytes
- Signed bundle SHA-256:
  `f5620858e9983f8272eceb4d3dc93afee7b906cc6e8335e8280b1ceed5bcf9a5`
- Toolchain: ESP-IDF v6.0.2

The installed M2 candidate passed the signed public pull OTA, opposite-slot
boot, USB0 mount and direct FLX4 profile/MIDI/UAC startup without a new TWDT or
OTA error. Its audio predecessor completed 126 fully sampled CUE/restart
transitions with zero strict counter delta. A subsequent fresh combined soak
passed for 180.156 minutes and 60 scheduled operations with zero strict counter
delta, no reboot/TWDT and operator-confirmed clean audio throughout. Evidence:
[`validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md`](validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md)
and
[`validation/P4_FINAL_COMBINED_SOAK_20260920.md`](validation/P4_FINAL_COMBINED_SOAK_20260920.md).
The current 324-track media fixture additionally passed complete real WAV and
FLAC natural EOF plus simultaneous MP3+FLAC and MP3+WAV playback with audible
operator acceptance and flat focused fault deltas. See
[`validation/P4_BOUNDED_MEDIA_CACHE_20260919.md`](validation/P4_BOUNDED_MEDIA_CACHE_20260919.md).
Every accepted cycle retained the 100-track Library, advanced both decks for
more than five seconds, restored controller/UAC and passed audible MAIN/cue;
drop, overflow, packet-loss, PCM-underrun and output-late deltas stayed zero.
Lifecycle accounting is complete: 43/50 PASS, 7/50 explicitly waived in Groups
I/J and zero pending. The accepted runs include 39 physical
attachment/reconnect actions. Groups I/J are permanently closed by operator
decision. The earlier
`RC2-116-g77d723c` remained on
boot epoch 389 through its
targeted three-hour continuous dual-MP3 limiter/WDT soak, with no watchdog
reset, PCM underrun or active UAC loss and no observable output failure,
controller disconnect or USB host daemon error. Fourteen output-late warnings
over 1,999,090 submitted UAC blocks did not correlate with a downstream
failure; no source change is justified from that count alone. See
[`validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md`](validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md).

The installed candidate contains deterministic Group G audio-load removal
support. Its guarded loader gate pauses after the first bounded 8 KiB PDB read,
and the updated lifecycle harness alternates the target deck across
G1--G5. The gate unit test, harness self-test, complete P4 host suite, clean
ESP-IDF v6.0.2 build, signed-package verification, OTA and focused smoke pass.
G1--G5 also pass on boot 416: every cycle restored 100 tracks, advanced both
decks for more than ten seconds, retained FLX4 MIDI/UAC, matched all recovery
requests to successes and produced zero critical fault deltas.

H1--H4 passed on boot 416 and H5 passed after a clean controlled reboot on
boot 417. Each accepted cycle disconnected only FLX4, retained USB0 plus the
coherent 100-track Library, observed exactly one controller disconnect/connect
and restored profile, MIDI, LEDs, UAC, dual playback and audible MAIN/cue with
zero accepted-cycle critical fault deltas. Operator-invalid attempts involving
an unintended cable action or USB0 removal do not count.

Earlier focused evidence remains valid within its stated limits:

- [`validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md`](validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md)
  — one USB0 remove/reinsert with FLX4 active;
- [`validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md`](validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md)
  — one FLX4 reconnect with USB0 retained and post-reconnect dual playback;
- [`validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md`](validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md)
  — 30-minute dual-active MP3 soak with seven seek/restart cycles.

## Release status

The branch is **not release-qualified and must not be merged yet**. The code,
host tests, signed build, lifecycle matrix and focused product paths are healthy;
the critical path is now the remaining release qualification:

1. finish the physically verified MP3/WAV/FLAC seek, loop, CUE, scratch and
   near-EOF acceptance matrix (focused load/play/EOF and mixed playback pass);
2. on-device Master Tempo CPU/I2S deadline and listening-quality acceptance;
3. guarded P4 web, profile and remaining pull/push OTA fault/recovery matrix;
4. complete the remaining interrupted-transfer and signed-rollback portions of
   the reduced OTA/fault matrix; the `RC2` to `M2` migration and positive public
   pull path pass;
5. closed-enclosure power, thermal, RF and wired-recovery acceptance, including
   repetition of the passed bench electrical measurements;
6. production credential, signing-key, rotation and optional irreversible
   security decisions;
7. exact final-candidate full functional smoke, documentation freeze and tag.

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
