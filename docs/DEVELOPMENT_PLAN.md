# P4 Development Plan

Status: **active P4-only plan, reconciled 2026-09-20**.

## Current position

The direct dual-root product path is implemented:

- USB0 hosts Rekordbox media;
- USB1 hosts the DDJ-FLX4 MIDI and four-channel USB audio interfaces;
- P4 owns controller state, playback, UI, LEDs, MAIN and cue audio;
- immutable tagged `M2` at `d2dabfa` is the installed candidate on `ota_0`;
  its full host suite, clean ESP-IDF v6.0.2 signed build, package verification,
  public pull OTA and focused hardware regression pass;
- the 78.176-minute combined functional gate is PASS with explicit clean-audio
  confirmation. The first following three-hour combined soak stopped after
  69.187 minutes on `+18,316` UAC underflow frames. The failure was reproduced
  in one focused pre-fix cycle and traced to the output task not feeding silence
  while both renderers were temporarily idle during CUE/restart. The installed
  repair keeps MAIN/UAC clocking through that transition and has zero strict
  deltas across 126 fully sampled focused transitions. The fresh combined soak
  then passed for 180.156 minutes and 60 operations with zero strict counter
  delta, no reboot/TWDT and operator-confirmed clean audio throughout;
- the bounded-cache LRU defect is fixed and exact-image verified: complete real
  96 kHz/24-bit FLAC and PCM16 WAV files reached natural EOF without PCM/BNA
  failure, simultaneous MP3+FLAC and MP3+WAV 30-second windows had zero locked
  read, underrun, late and BNA deltas, and both mixed pairs received explicit
  audible acceptance; the remaining seek/loop/CUE/scratch edges keep Phase 4
  open;
- deterministic Group F support is implemented and hardware-qualified: the PDB
  reader releases media ownership after at most 8 KiB, incomplete rebuilds
  fail closed, and a guarded one-shot validation barrier lets the harness
  request removal after the first PDB read instead of relying on operator
  timing; host tests, the ESP-IDF 6.0.2 signed build, exact-image OTA smoke and
  all five Group F hardware cycles pass;
- the earlier `RC2-116-g77d723c` targeted three-hour continuous dual-MP3
  limiter/WDT soak passed without
  reset, underrun, active UAC loss or USB/controller loss;
- 14 rare output-late warnings were investigated as bounded I2S
  pacing/scheduler jitter with no downstream failure, so no code change was
  made;
- the current bench common 5 V and dual-VBUS measurement gate is
  operator-confirmed PASS;
- the 30-minute exact-image dual-active MP3 gate is closed;
- lifecycle Groups A--H pass: 36/50 controlled cycles and 36 planned physical
  attachment/reconnect actions are complete. Deterministic Group G audio-load
  removal alternated D1/D2 across five cycles; every remount restored 100
  tracks, all recovery requests matched successes, and controller/audio fault
  deltas stayed zero. Five idle USB1 reconnects then retained USB0 and its
  100-track Library and restored FLX4 profile, MIDI, LEDs, UAC and audible
  MAIN/cue. The accelerated paired Group I/J harness is implemented and
  self-tested. Its first valid Group I run preserved playback, USB0, Library,
  controller and audible audio, but exposed a false UAC data-loss latch caused
  by the reconnect-prime underflow being charged to the old playback session.
  UAC stream-epoch accounting is implemented and exact-image confirmed: I1,
  I2 and J1 passed on boot 420 with zero critical fault deltas. By explicit
  operator decision, I3--I5 and J2--J5 are waived and permanently closed;
- the guarded Group K software-reboot endpoint and deterministic K1/K2 harness
  are implemented and pass the complete host suite and ESP-IDF v6.0.2 firmware
  build. Exact-image K1 and K2 passed on boots 424 and 426 with `SW` reset,
  unchanged version/slot, automatic USB0/FLX4 and 100-track Library recovery,
  clean fault evidence, dual playback and audible MAIN/cue confirmation. The
  first K2 launch also passed technically on boot 425 but is not counted because
  the shared helper overwrote its evidence label; the harness now preserves and
  self-tests the requested cycle identity;
- the deterministic Group L harness verifies the local ECDSA-signed bundle,
  performs a real push OTA, requires the inactive OTA slot to become active and
  reuses the strict Group K USB/Library/audio acceptance. Its PowerShell 7 and
  Windows PowerShell 5.1 self-tests pass. L1 and L2 passed on boots 427 and 428,
  alternating `ota_1 -> ota_0 -> ota_1` while restoring both USB roots, 100
  tracks, dual playback, MAIN/cue, LEDs and controls without manual reinsert or
  critical fault evidence.

No further architecture conversion is planned. Remaining work is ordered
release qualification, with implementation only when a measured gate exposes a
real defect. Exact cache evidence is in
[`validation/P4_BOUNDED_MEDIA_CACHE_20260919.md`](validation/P4_BOUNDED_MEDIA_CACHE_20260919.md).

## Ordered remaining phases

The lifecycle matrix is fully accounted: 43/50 PASS,
seven explicitly waived I/J cycles, zero pending cycles and 39 accepted physical
attachment/reconnect actions. Groups I/J are administratively closed and will
not be resumed. The combined functional session and repaired three-hour soak
are also complete. The RC2-to-M2 prefix migration, public positive pull-OTA,
reduced negative/recovery OTA matrix and final M2 beta acceptance are complete.
The feature was merged into `master` at `d3099f9`; post-merge CI passed at
`c786de7`. Remaining work belongs only to the unrestricted production release.
Detailed scope and beta versus production boundaries are in
[`M2_BETA_ACCELERATED_RELEASE_PLAN.md`](M2_BETA_ACCELERATED_RELEASE_PLAN.md).

The release/version prefix migration from `RC2` to `M2` is complete as an
atomic source transition. Commit `d2dabfa` added family-aware newer-only OTA
ordering and was first installed as the signed `RC2-156-gd2dabfa` bridge. The
same commit was then tagged `M2`, rebuilt cleanly, published through the
canonical HTTPS channel and installed by the device's pull-OTA path. The device
booted `M2` from the opposite slot with USB0 and FLX4 MIDI/UAC healthy.

| Phase | Work | Exit criterion |
| --- | --- | --- |
| 1 | Publish the P4-only source of truth, archive legacy active gates and record `c8b2711` versus the installed baseline | Active documents are P4-only, links pass and remote SHA matches |
| 2 | Qualify common 5 V and both downstream VBUS branches | Measured startup/sustained margins, no backfeed, current limiting/protection documented |
| 3 | Run complete dual-USB lifecycle matrix | Cold/warm boots, both insertion orders, repeated USB0/USB1 reconnect, active-decode removal and reboot recovery pass |
| 4 | Qualify bounded media cache with real files | Verified MP3/WAV/FLAC fixtures run simultaneously without audible artefact, locked read or recovery-counter failure |
| 5 | Measure P4 real-time audio margin | Worst-case dual Master Tempo, pitch/sample-rate, scratch and MAIN/cue deadlines remain inside budget |
| 6 | Close functional DSP/FX rows | Near-EOF, STOP/LOAD, mixer/PFL and detailed Beat FX transition/routing checks pass |
| 7 | Close Wi-Fi, web, profile and OTA fault paths | Pull/push OTA, rollback, invalid/slow/interrupted requests and post-reboot USB recovery pass |
| 8 | Run multi-hour combined soak | Defined multi-hour run completes without reset, media/controller loss, latched control or gated counter increase |
| 9 | Qualify final enclosure | Accepted for this scope by operator after approximately two months in the existing enclosure; wired recovery confirmed; repeat after physical/power changes |
| 10 | Resolve production security | Policy complete for M2.1: shared credential accepted, WPA2/WPA3 transition mode plus PMF capability, encrypted offline key custody and backup, defined rotation boundary, SBOM waived, and irreversible P4 security deferred on the sole board; physical key-store verification remains operationally open |
| 11 | Freeze and release | Freeze exact commit, run pre-tag gates, create local `M2.1` tag, build/sign/install and smoke the exact tagged artifact, then publish the validation record, channel and immutable tag |

## Test policy

- Use ESP-IDF v6.0.2 only.
- Do not optimize DSP without a failing on-device deadline measurement.
- Do not infer WAV/FLAC acceptance from database rows; verify the files exist.
- Treat HTTP upload success as transport evidence, not boot or functional
  validity.
- Use the stricter lifecycle requirement when documents disagree: target 50
  controlled USB lifecycle cycles, with at least 20 independent physical
  reconnects and explicit active-load/decode removals.
- Preserve raw counters, firmware version, slot, boot epoch and operator-visible
  or audible results for every acceptance session.
- Do not move the immutable `M2` tag. Production release must use a new version
  identifying its exact source commit. The selected production version is
  `M2.1`; create it locally only after the frozen commit passes pre-tag gates,
  and do not push it before final exact-image acceptance.
- Apply [`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md):
  no Secure Boot, Flash Encryption or security eFuse burn on the sole P4 board.

## Deferred work

Non-FLX4 controllers, recorder re-enable, LIBAPTA integration and new optional
features remain separate projects after the first FLX4 release.

The executable session plan and evidence fields are in
[`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).
